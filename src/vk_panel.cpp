#include "vk_panel.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "canvas.h"
#include "vk_util.h"

static std::vector<std::string> split(const std::string &s) { // at spaces
    std::vector<std::string> out;
    for (size_t i = 0, j = 0; i < s.size(); i = j + 1) {
        j = s.find(' ', i);
        if (j == std::string::npos) j = s.size();
        if (j > i) out.push_back(s.substr(i, j - i));
    }
    return out;
}

uint32_t VkPanel::mem_type(uint32_t bits, VkMemoryPropertyFlags want) const {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((bits >> i & 1) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
    return 0;
}

bool VkPanel::init() {
    char b[4096] = "";
    vr::VRCompositor()->GetVulkanInstanceExtensionsRequired(b, sizeof b);
    std::vector<std::string> ie = split(b);
    std::vector<const char *> iep;
    iep.reserve(ie.size());
    for (const auto &e : ie) iep.push_back(e.c_str());
    VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    ai.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &ai;
    ici.enabledExtensionCount = static_cast<uint32_t>(iep.size());
    ici.ppEnabledExtensionNames = iep.data();
    if (vkCreateInstance(&ici, nullptr, &inst) != VK_SUCCESS) return false;
    uint64_t out = 0;
    vr::VRSystem()->GetOutputDevice(&out, vr::TextureType_Vulkan, inst);
    pd = reinterpret_cast<VkPhysicalDevice>(out);
    if (!pd) {
        uint32_t n = 1;
        vkEnumeratePhysicalDevices(inst, &n, &pd);
    }
    if (!pd) return false;
    b[0] = 0;
    vr::VRCompositor()->GetVulkanDeviceExtensionsRequired(pd, b, sizeof b);
    std::vector<std::string> de = split(b);
    std::vector<const char *> dep;
    dep.reserve(de.size());
    for (const auto &e : de) dep.push_back(e.c_str());
    qf = graphics_qfam(pd);
    if (qf == UINT32_MAX) return false;
    float prio = 1;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = qf;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = static_cast<uint32_t>(dep.size());
    dci.ppEnabledExtensionNames = dep.data();
    if (vkCreateDevice(pd, &dci, nullptr, &dev) != VK_SUCCESS) return false;
    vkGetDeviceQueue(dev, qf, 0, &q);

    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_R8G8B8A8_UNORM;
    ii.extent = {kTex, kTex, 1};
    ii.mipLevels = ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (vkCreateImage(dev, &ii, nullptr, &img) != VK_SUCCESS) return false;
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(dev, img, &mr);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = mem_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkDeviceMemory im = nullptr;
    if (vkAllocateMemory(dev, &mai, nullptr, &im) != VK_SUCCESS) return false;
    vkBindImageMemory(dev, img, im, 0);

    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = static_cast<VkDeviceSize>(kTex) * kTex * 4;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (vkCreateBuffer(dev, &bi, nullptr, &buf) != VK_SUCCESS) return false;
    vkGetBufferMemoryRequirements(dev, buf, &mr);
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex =
        mem_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory bm = nullptr;
    if (vkAllocateMemory(dev, &mai, nullptr, &bm) != VK_SUCCESS) return false;
    vkBindBufferMemory(dev, buf, bm, 0);
    vkMapMemory(dev, bm, 0, bi.size, 0, &map);

    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = qf;
    VkCommandPool pool = nullptr;
    vkCreateCommandPool(dev, &pci, nullptr, &pool);
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool;
    cai.commandBufferCount = 1;
    vkAllocateCommandBuffers(dev, &cai, &cb);
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    vkCreateFence(dev, &fci, nullptr, &fence);
    return true;
}

void VkPanel::upload(const std::vector<unsigned char> &px, vr::VROverlayHandle_t h) {
    vkQueueWaitIdle(q); // the previous copy by SteamVR is done
    memcpy(map, px.data(), px.size());
    VkCommandBufferBeginInfo bbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkResetCommandBuffer(cb, 0);
    vkBeginCommandBuffer(cb, &bbi);
    VkImageMemoryBarrier bar{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    bar.oldLayout = first ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    bar.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    bar.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    bar.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    bar.srcQueueFamilyIndex = bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar.image = img;
    bar.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                         1, &bar);
    VkBufferImageCopy reg{};
    reg.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    reg.imageExtent = {kTex, kTex, 1};
    vkCmdCopyBufferToImage(cb, buf, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &reg);
    bar.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    bar.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; // what SteamVR expects
    bar.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    bar.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                         1, &bar);
    vkEndCommandBuffer(cb);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    vkQueueSubmit(q, 1, &si, fence);
    vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX);
    vkResetFences(dev, 1, &fence);
    first = false;
    vr::VRVulkanTextureData_t d{
        reinterpret_cast<uint64_t>(img), dev, pd, inst, q, qf, static_cast<uint32_t>(kTex), static_cast<uint32_t>(kTex),
        VK_FORMAT_R8G8B8A8_UNORM,        1};
    vr::Texture_t t{&d, vr::TextureType_Vulkan, vr::ColorSpace_Auto};
    vr::VROverlay()->SetOverlayTexture(h, &t);
}

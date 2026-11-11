// The panel image as a Vulkan texture for the SteamVR overlay.
#pragma once

#include <vulkan/vulkan.h>
#include <openvr.h>

#include <cstdint>
#include <vector>

// The panel image as a Vulkan texture: SetOverlayRaw blinks on every update. The client library copies the image
// during SetOverlayTexture, on our queue, so one image is enough.
struct VkPanel {
    VkInstance inst = VK_NULL_HANDLE;
    VkPhysicalDevice pd = VK_NULL_HANDLE;
    VkDevice dev = VK_NULL_HANDLE;
    VkQueue q = VK_NULL_HANDLE;
    uint32_t qf = 0;
    VkImage img = VK_NULL_HANDLE;
    VkBuffer buf = VK_NULL_HANDLE;
    VkCommandBuffer cb = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    void *map = nullptr;
    bool first = true;
    bool init(); // false if Vulkan is not usable: use SetOverlayRaw instead
    void upload(const std::vector<unsigned char> &px, vr::VROverlayHandle_t h); // kTex x kTex RGBA to the overlay
  private:
    [[nodiscard]] uint32_t mem_type(uint32_t bits, VkMemoryPropertyFlags want) const;
};

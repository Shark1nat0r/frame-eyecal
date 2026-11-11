#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

inline uint32_t graphics_qfam(VkPhysicalDevice pd) { // the first queue family with graphics, UINT32_MAX if none
    uint32_t n = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &n, nullptr);
    std::vector<VkQueueFamilyProperties> qfp(n);
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &n, qfp.data());
    for (uint32_t i = 0; i < n; i++)
        if (qfp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) return i;
    return UINT32_MAX;
}

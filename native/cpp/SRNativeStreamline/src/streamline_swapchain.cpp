/*
 * Super Resolution — Streamline Integration
 * Copyright (c) 2025-2026. 187J3X1-114514
 *
 * Vulkan swapchain creation/management + slUpgradeInterface hooking.
 * This is the Phase 3 implementation that replaces glfwSwapBuffers with
 * a Vulkan present path intercepted by Streamline for DLSS-G.
 *
 * NOTE: Full implementation requires VulkanPresenter.java and the
 * WindowMixin intercept to be completed on the Java side.
 */

#define SR_STREAMLINE_EXPORTS
#include "sr/streamline_api.h"

#include <sl.h>
#include <sl_core_api.h>
#include <sl_hooks.h>

#include <vulkan/vulkan.h>

#include <cstdio>
#include <cstring>
#include <vector>

// ── External state from streamline_core.cpp ──
extern bool g_slInitialized;

// ── Swapchain state ──
static VkSwapchainKHR g_swapchain = VK_NULL_HANDLE;
static VkSurfaceKHR g_surface = VK_NULL_HANDLE;
static VkDevice g_device = VK_NULL_HANDLE;
static VkInstance g_instance = VK_NULL_HANDLE;
static VkPhysicalDevice g_physicalDevice = VK_NULL_HANDLE;
static uint32_t g_presentQueueFamily = 0;
static VkQueue g_presentQueue = VK_NULL_HANDLE;

static std::vector<VkImage> g_swapchainImages;
static std::vector<VkImageView> g_swapchainImageViews;
static uint32_t g_width = 0, g_height = 0;
static VkFormat g_imageFormat = VK_FORMAT_B8G8R8A8_UNORM;


// ── Helper: Choose surface format ──
static VkSurfaceFormatKHR chooseSurfaceFormat(VkPhysicalDevice physDev, VkSurfaceKHR surface) {
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physDev, surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physDev, surface, &count, formats.data());

    // Prefer B8G8R8A8_SRGB or B8G8R8A8_UNORM
    for (auto& fmt : formats) {
        if (fmt.format == VK_FORMAT_B8G8R8A8_SRGB &&
            fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return fmt;
        }
    }
    for (auto& fmt : formats) {
        if (fmt.format == VK_FORMAT_B8G8R8A8_UNORM) {
            return fmt;
        }
    }
    return formats[0];
}

// ── Helper: Choose present mode ──
static VkPresentModeKHR choosePresentMode(VkPhysicalDevice physDev, VkSurfaceKHR surface) {
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physDev, surface, &count, nullptr);
    std::vector<VkPresentModeKHR> modes(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physDev, surface, &count, modes.data());

    // Prefer MAILBOX (triple buffer, no tearing) for DLSS-G
    for (auto mode : modes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) return mode;
    }
    return VK_PRESENT_MODE_FIFO_KHR; // Always available
}

// ── Helper: destroy image views ──
static void destroyImageViews() {
    for (auto view : g_swapchainImageViews) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(g_device, view, nullptr);
        }
    }
    g_swapchainImageViews.clear();
}

// ── Helper: create image views for swapchain images ──
static bool createImageViews() {
    destroyImageViews();
    g_swapchainImageViews.resize(g_swapchainImages.size(), VK_NULL_HANDLE);

    for (size_t i = 0; i < g_swapchainImages.size(); i++) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = g_swapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = g_imageFormat;
        viewInfo.components = { VK_COMPONENT_SWIZZLE_IDENTITY,
                                VK_COMPONENT_SWIZZLE_IDENTITY,
                                VK_COMPONENT_SWIZZLE_IDENTITY,
                                VK_COMPONENT_SWIZZLE_IDENTITY };
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;

        VkResult res = vkCreateImageView(g_device, &viewInfo, nullptr,
                                          &g_swapchainImageViews[i]);
        if (res != VK_SUCCESS) {
            fprintf(stderr, "[SL ERROR] vkCreateImageView[%zu] failed: %d\n", i, res);
            return false;
        }
    }
    return true;
}


// ════════════════════════════════════════════════════════════════════════
// Public API
// ════════════════════════════════════════════════════════════════════════

extern "C" SL_BRIDGE_API int slBridge_CreateSwapchain(
    uint64_t vkInstance,
    uint64_t vkPhysicalDevice,
    uint64_t vkDevice,
    uint64_t vkSurfaceKHR,
    uint32_t width,
    uint32_t height,
    uint32_t imageFormat,
    uint32_t presentQueueFamily
) {
    if (!g_slInitialized) return -1;

    g_instance = (VkInstance)vkInstance;
    g_physicalDevice = (VkPhysicalDevice)vkPhysicalDevice;
    g_device = (VkDevice)vkDevice;
    g_surface = (VkSurfaceKHR)vkSurfaceKHR;
    g_width = width;
    g_height = height;
    g_presentQueueFamily = presentQueueFamily;

    // Get present queue
    vkGetDeviceQueue(g_device, presentQueueFamily, 0, &g_presentQueue);

    // Query surface capabilities
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_physicalDevice, g_surface, &caps);

    VkSurfaceFormatKHR surfFmt = chooseSurfaceFormat(g_physicalDevice, g_surface);
    g_imageFormat = (imageFormat != 0) ? (VkFormat)imageFormat : surfFmt.format;

    VkPresentModeKHR presentMode = choosePresentMode(g_physicalDevice, g_surface);

    // Clamp extent
    VkExtent2D extent = { width, height };
    extent.width = std::max(caps.minImageExtent.width,
                   std::min(caps.maxImageExtent.width, extent.width));
    extent.height = std::max(caps.minImageExtent.height,
                    std::min(caps.maxImageExtent.height, extent.height));

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) {
        imageCount = caps.maxImageCount;
    }

    // Create swapchain
    VkSwapchainCreateInfoKHR swapInfo{};
    swapInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapInfo.surface = g_surface;
    swapInfo.minImageCount = imageCount;
    swapInfo.imageFormat = g_imageFormat;
    swapInfo.imageColorSpace = surfFmt.colorSpace;
    swapInfo.imageExtent = extent;
    swapInfo.imageArrayLayers = 1;
    swapInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swapInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapInfo.preTransform = caps.currentTransform;
    swapInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapInfo.presentMode = presentMode;
    swapInfo.clipped = VK_TRUE;
    swapInfo.oldSwapchain = g_swapchain; // For recreation

    VkResult vkRes = vkCreateSwapchainKHR(g_device, &swapInfo, nullptr, &g_swapchain);
    if (vkRes != VK_SUCCESS) {
        fprintf(stderr, "[SL ERROR] vkCreateSwapchainKHR failed: %d\n", vkRes);
        return (int)vkRes;
    }

    // Destroy old swapchain if recreating
    if (swapInfo.oldSwapchain != VK_NULL_HANDLE) {
        destroyImageViews();
        vkDestroySwapchainKHR(g_device, swapInfo.oldSwapchain, nullptr);
    }

    // SL: Upgrade the swapchain interface for DLSS-G interception
    sl::Result slRes = slUpgradeInterface(reinterpret_cast<void**>(&g_swapchain));
    if (slRes != sl::Result::eOk) {
        fprintf(stderr, "[SL WARN] slUpgradeInterface for swapchain failed: %d "
                        "(DLSS-G may not work)\n", (int)slRes);
        // Non-fatal: swapchain still works, just no FG
    }

    // Get swapchain images
    uint32_t imgCount = 0;
    vkGetSwapchainImagesKHR(g_device, g_swapchain, &imgCount, nullptr);
    g_swapchainImages.resize(imgCount);
    vkGetSwapchainImagesKHR(g_device, g_swapchain, &imgCount, g_swapchainImages.data());

    // Create image views
    if (!createImageViews()) {
        return -2;
    }

    g_width = extent.width;
    g_height = extent.height;

    fprintf(stderr, "[SL INFO] Swapchain created: %ux%u, %u images, format=%d\n",
            g_width, g_height, imgCount, g_imageFormat);
    return 0;
}

extern "C" SL_BRIDGE_API int slBridge_DestroySwapchain() {
    if (g_device == VK_NULL_HANDLE) return 0;

    // Wait for GPU idle before destruction
    vkDeviceWaitIdle(g_device);

    destroyImageViews();
    g_swapchainImages.clear();

    if (g_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(g_device, g_swapchain, nullptr);
        g_swapchain = VK_NULL_HANDLE;
    }

    if (g_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(g_instance, g_surface, nullptr);
        g_surface = VK_NULL_HANDLE;
    }

    fprintf(stderr, "[SL INFO] Swapchain destroyed\n");
    return 0;
}

extern "C" SL_BRIDGE_API int slBridge_AcquireNextImage(
    uint64_t imageAvailableSemaphore,
    uint32_t* outImageIndex
) {
    if (g_swapchain == VK_NULL_HANDLE || !outImageIndex) return -1;

    VkResult res = vkAcquireNextImageKHR(
        g_device, g_swapchain, UINT64_MAX,
        (VkSemaphore)imageAvailableSemaphore, VK_NULL_HANDLE, outImageIndex
    );

    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) {
        // Swapchain needs recreation — caller should handle
        return (int)res;
    }
    if (res != VK_SUCCESS) {
        fprintf(stderr, "[SL ERROR] vkAcquireNextImageKHR failed: %d\n", res);
        return (int)res;
    }
    return 0;
}

extern "C" SL_BRIDGE_API int slBridge_Present(
    uint32_t imageIndex,
    uint64_t* waitSemaphores,
    uint32_t numWaitSemaphores
) {
    if (g_swapchain == VK_NULL_HANDLE || g_presentQueue == VK_NULL_HANDLE) return -1;

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &g_swapchain;
    presentInfo.pImageIndices = &imageIndex;
    presentInfo.waitSemaphoreCount = numWaitSemaphores;
    presentInfo.pWaitSemaphores = (VkSemaphore*)waitSemaphores;

    // This call is intercepted by Streamline when DLSS-G is active
    VkResult res = vkQueuePresentKHR(g_presentQueue, &presentInfo);

    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) {
        return (int)res;
    }
    if (res != VK_SUCCESS) {
        fprintf(stderr, "[SL ERROR] vkQueuePresentKHR failed: %d\n", res);
        return (int)res;
    }
    return 0;
}

extern "C" SL_BRIDGE_API int slBridge_RecreateSwapchain(uint32_t width, uint32_t height) {
    if (g_device == VK_NULL_HANDLE || g_surface == VK_NULL_HANDLE) return -1;

    vkDeviceWaitIdle(g_device);

    // Re-use CreateSwapchain with the old swapchain set
    return slBridge_CreateSwapchain(
        (uint64_t)g_instance, (uint64_t)g_physicalDevice, (uint64_t)g_device,
        (uint64_t)g_surface, width, height, (uint32_t)g_imageFormat,
        g_presentQueueFamily
    );
}

extern "C" SL_BRIDGE_API int slBridge_GetSwapchainImages(uint64_t* outImages, uint32_t* outCount) {
    if (!outCount) return -1;

    uint32_t available = (uint32_t)g_swapchainImages.size();
    if (!outImages) {
        *outCount = available;
        return 0;
    }

    uint32_t toWrite = std::min(*outCount, available);
    for (uint32_t i = 0; i < toWrite; i++) {
        outImages[i] = (uint64_t)g_swapchainImages[i];
    }
    *outCount = toWrite;
    return 0;
}


// ════════════════════════════════════════════════════════════════════════
// Future: DLSS SR via Streamline (stubs)
// ════════════════════════════════════════════════════════════════════════

extern "C" SL_BRIDGE_API int slBridge_DLSSSetOptions(
    uint32_t viewportId, int mode,
    uint32_t outputWidth, uint32_t outputHeight,
    float sharpness, int colorBuffersHDR, int useAutoExposure
) {
    // TODO: Implement when migrating DLSS SR from NGX to SL
    fprintf(stderr, "[SL] slBridge_DLSSSetOptions not yet implemented\n");
    return -1;
}

extern "C" SL_BRIDGE_API int slBridge_DLSSGetOptimalSettings(
    int mode, uint32_t outputWidth, uint32_t outputHeight,
    uint32_t* outRenderWidth, uint32_t* outRenderHeight, float* outSharpness
) {
    // TODO: Implement when migrating DLSS SR from NGX to SL
    fprintf(stderr, "[SL] slBridge_DLSSGetOptimalSettings not yet implemented\n");
    return -1;
}

extern "C" SL_BRIDGE_API int slBridge_DLSSGetState(
    uint32_t viewportId, uint64_t* outEstimatedVRAM
) {
    // TODO: Implement when migrating DLSS SR from NGX to SL
    return -1;
}

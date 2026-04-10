/*
 * Super Resolution — Streamline Integration
 * Copyright (c) 2025-2026. 187J3X1-114514
 *
 * Core Streamline lifecycle: slInit, slShutdown, slSetVulkanInfo, FrameToken.
 */

#define SR_STREAMLINE_EXPORTS
#include "sr/streamline_api.h"

#include <sl.h>
#include <sl_consts.h>
#include <sl_core_api.h>
#include <sl_core_types.h>
#include <vulkan/vulkan.h>
#include <sl_helpers_vk.h>
#include <sl_reflex.h>
#include <sl_pcl.h>
#include <sl_dlss_g.h>

#include <cstring>
#include <cstdio>
#include <mutex>

// ── Globals (shared across TUs via extern in other .cpp files) ──
bool g_slInitialized = false;
static std::mutex g_slMutex;

// ── Log callback forwarding ──
static void slLogCallback(sl::LogType type, const char* msg) {
    const char* prefix = "[SL]";
    switch (type) {
        case sl::LogType::eLogTypeInfo:    prefix = "[SL INFO]"; break;
        case sl::LogType::eLogTypeWarn:    prefix = "[SL WARN]"; break;
        case sl::LogType::eLogTypeError:   prefix = "[SL ERROR]"; break;
        default: break;
    }
    fprintf(stderr, "%s %s\n", prefix, msg);
}


// ════════════════════════════════════════════════════════════════════════
// Phase 1: Core Lifecycle
// ════════════════════════════════════════════════════════════════════════

extern "C" SL_BRIDGE_API int slBridge_Init(
    const wchar_t* pluginsPath,
    uint64_t appId,
    int logLevel
) {
    std::lock_guard<std::mutex> lock(g_slMutex);
    if (g_slInitialized) {
        return 0; // Already initialized
    }

    sl::Preferences prefs{};
    prefs.showConsole = (logLevel >= 2);
    prefs.logLevel = (logLevel == 0) ? sl::LogLevel::eLogLevelOff :
                     (logLevel == 1) ? sl::LogLevel::eLogLevelDefault :
                                       sl::LogLevel::eLogLevelVerbose;
    prefs.logMessageCallback = slLogCallback;

    // Plugin paths
    const wchar_t* paths[] = { pluginsPath };
    prefs.pathsToPlugins = paths;
    prefs.numPathsToPlugins = 1;

    prefs.applicationId = appId;
    prefs.renderAPI = sl::RenderAPI::eVulkan;

    // Load all features we may use (SL ignores missing DLLs gracefully)
    sl::Feature features[] = {
        sl::kFeatureReflex,
        sl::kFeaturePCL,
        sl::kFeatureDLSS_G,
        sl::kFeatureDLSS,       // Future: DLSS SR via SL
        sl::kFeatureDLSS_RR,    // Future: DLSS Ray Reconstruction
        sl::kFeatureNIS,        // Future: NIS via SL
        sl::kFeatureDeepDVC,    // Future: DeepDVC
    };
    prefs.featuresToLoad = features;
    prefs.numFeaturesToLoad = sizeof(features) / sizeof(features[0]);

    // Flags: manual hooking + frame-based tagging + host manages CL state
    prefs.flags = sl::PreferenceFlags::eUseManualHooking
                | sl::PreferenceFlags::eDisableCLStateTracking
                | sl::PreferenceFlags::eUseFrameBasedResourceTagging
#ifndef NDEBUG
                | sl::PreferenceFlags::eAllowUnsignedPlugins
#endif
                ;

    sl::Result res = slInit(prefs);
    if (res != sl::Result::eOk) {
        fprintf(stderr, "[SL ERROR] slInit failed: %d\n", (int)res);
        return (int)res;
    }

    g_slInitialized = true;
    fprintf(stderr, "[SL INFO] Streamline initialized successfully\n");
    return 0;
}

extern "C" SL_BRIDGE_API int slBridge_Shutdown() {
    std::lock_guard<std::mutex> lock(g_slMutex);
    if (!g_slInitialized) {
        return 0;
    }

    sl::Result res = slShutdown();
    g_slInitialized = false;

    if (res != sl::Result::eOk) {
        fprintf(stderr, "[SL ERROR] slShutdown failed: %d\n", (int)res);
        return (int)res;
    }

    fprintf(stderr, "[SL INFO] Streamline shutdown complete\n");
    return 0;
}

extern "C" SL_BRIDGE_API int slBridge_SetVulkanInfo(
    uint64_t vkInstance,
    uint64_t vkPhysicalDevice,
    uint64_t vkDevice,
    uint32_t graphicsQueueFamily,
    uint32_t graphicsQueueIndex
) {
    if (!g_slInitialized) return -1;

    sl::VulkanInfo vkInfo{};
    vkInfo.device = (VkDevice)vkDevice;
    vkInfo.instance = (VkInstance)vkInstance;
    vkInfo.physicalDevice = (VkPhysicalDevice)vkPhysicalDevice;

    // Compute queue — use same as graphics for now (single-queue setup)
    vkInfo.computeQueueIndex = graphicsQueueIndex;
    vkInfo.computeQueueFamily = graphicsQueueFamily;
    vkInfo.graphicsQueueIndex = graphicsQueueIndex;
    vkInfo.graphicsQueueFamily = graphicsQueueFamily;

    sl::Result res = slSetVulkanInfo(vkInfo);
    if (res != sl::Result::eOk) {
        fprintf(stderr, "[SL ERROR] slSetVulkanInfo failed: %d\n", (int)res);
        return (int)res;
    }

    fprintf(stderr, "[SL INFO] Vulkan device registered with Streamline\n");
    return 0;
}

extern "C" SL_BRIDGE_API int slBridge_IsFeatureSupported(uint32_t featureId) {
    if (!g_slInitialized) return 0;

    sl::AdapterInfo adapterInfo{};
    // No LUID needed when using Vulkan — SL uses the registered device
    sl::Result res = slIsFeatureSupported((sl::Feature)featureId, adapterInfo);
    return (res == sl::Result::eOk) ? 1 : 0;
}

extern "C" SL_BRIDGE_API int slBridge_GetNewFrameToken(
    uint32_t frameIndex,
    uint64_t* outTokenHandle
) {
    if (!g_slInitialized || !outTokenHandle) return -1;

    sl::FrameToken* token = nullptr;
    sl::Result res = slGetNewFrameToken(token, &frameIndex);
    if (res != sl::Result::eOk) {
        fprintf(stderr, "[SL ERROR] slGetNewFrameToken failed: %d\n", (int)res);
        return (int)res;
    }

    *outTokenHandle = reinterpret_cast<uint64_t>(token);
    return 0;
}

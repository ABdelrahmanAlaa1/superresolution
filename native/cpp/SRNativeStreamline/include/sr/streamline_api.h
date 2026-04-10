/*
 * Super Resolution — Streamline Integration
 * Copyright (c) 2025-2026. 187J3X1-114514
 *
 * Streamline SDK bridge for Reflex, PCL, DLSS-G, and future SL features.
 * This header defines the complete native C API for the JNI bridge.
 */

#pragma once

#include <stdint.h>

#ifdef _WIN32
    #ifdef SR_STREAMLINE_EXPORTS
        #define SL_BRIDGE_API __declspec(dllexport)
    #else
        #define SL_BRIDGE_API __declspec(dllimport)
    #endif
#else
    #define SL_BRIDGE_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ════════════════════════════════════════════════════════════════════════
// Phase 1: Core Lifecycle
// ════════════════════════════════════════════════════════════════════════

/// Initialize Streamline SDK with manual hooking + frame-based tagging.
/// @param pluginsPath  Absolute path to the streamline/ folder containing SL DLLs
/// @param appId        NVIDIA Application ID (0 = development mode)
/// @param logLevel     0=off, 1=default, 2=verbose
/// @return 0 on success, non-zero on error
SL_BRIDGE_API int slBridge_Init(
    const wchar_t* pluginsPath,
    uint64_t appId,
    int logLevel
);

/// Shutdown Streamline SDK. Must be called before destroying VkDevice.
SL_BRIDGE_API int slBridge_Shutdown();

/// Register the existing Vulkan device with Streamline.
/// Must be called after slBridge_Init and after VkDevice creation.
SL_BRIDGE_API int slBridge_SetVulkanInfo(
    uint64_t vkInstance,
    uint64_t vkPhysicalDevice,
    uint64_t vkDevice,
    uint32_t graphicsQueueFamily,
    uint32_t graphicsQueueIndex
);

/// Check if a specific SL feature is supported on this system.
/// Feature IDs: 0=DLSS, 1=Reflex, 2=NIS, 3=DLSS_G, 5=DeepDVC, 1001=DLSS_RR
/// @return 1 if supported, 0 if not
SL_BRIDGE_API int slBridge_IsFeatureSupported(uint32_t featureId);

/// Obtain a new FrameToken for the given frame index.
/// The returned handle must be passed to all subsequent SL calls for this frame.
/// @param frameIndex  Monotonically increasing frame counter (from RenderHandlerManager)
/// @param outTokenHandle  Output: opaque token handle
/// @return 0 on success
SL_BRIDGE_API int slBridge_GetNewFrameToken(uint32_t frameIndex, uint64_t* outTokenHandle);


// ════════════════════════════════════════════════════════════════════════
// Phase 2: Reflex
// ════════════════════════════════════════════════════════════════════════

/// Set Reflex mode and optional frame limiter.
/// @param mode  0=Off, 1=LowLatency, 2=LowLatencyWithBoost
/// @param frameLimitUs  Frame limit in microseconds (0 = no limit)
SL_BRIDGE_API int slBridge_ReflexSetOptions(int mode, uint32_t frameLimitUs);

/// Perform Reflex sleep. Must be called every frame regardless of mode.
SL_BRIDGE_API int slBridge_ReflexSleep(uint64_t frameTokenHandle);

/// Get current Reflex state/stats.
/// @param outLowLatAvail   Output: 1 if low latency mode is available
/// @param outGpuActiveMs   Output: GPU active time in milliseconds
/// @param outGpuFrameMs    Output: GPU frame time in milliseconds
/// @param outPcLatencyMs   Output: total PC latency in milliseconds
SL_BRIDGE_API int slBridge_ReflexGetState(
    int* outLowLatAvail,
    float* outGpuActiveMs,
    float* outGpuFrameMs,
    float* outPcLatencyMs
);

/// Provide camera data for Reflex camera prediction (improves DLSS-G quality).
/// Matrices must be row-major.
SL_BRIDGE_API int slBridge_ReflexSetCameraData(
    uint64_t frameTokenHandle,
    const float* viewToClip4x4,
    const float* worldToView4x4
);

/// Get predicted camera data from Reflex (for latency-compensated rendering).
SL_BRIDGE_API int slBridge_ReflexGetPredictedCameraData(
    uint64_t frameTokenHandle,
    float* outPredictedViewToClip4x4,
    float* outPredictedWorldToView4x4
);


// ════════════════════════════════════════════════════════════════════════
// Phase 2: PCL (PC Latency) Markers
// ════════════════════════════════════════════════════════════════════════

/// PCL Marker types
enum SlBridgePCLMarker {
    SL_BRIDGE_PCL_SIMULATION_START = 0,
    SL_BRIDGE_PCL_SIMULATION_END = 1,
    SL_BRIDGE_PCL_RENDER_SUBMIT_START = 2,
    SL_BRIDGE_PCL_RENDER_SUBMIT_END = 3,
    SL_BRIDGE_PCL_PRESENT_START = 4,
    SL_BRIDGE_PCL_PRESENT_END = 5,
    SL_BRIDGE_PCL_INPUT_SAMPLE = 6,
    SL_BRIDGE_PCL_TRIGGER_FLASH = 7,
    SL_BRIDGE_PCL_PC_LATENCY_PING = 8
};

/// Set a PCL marker at the current point. Must be called every frame.
SL_BRIDGE_API int slBridge_PCLSetMarker(uint32_t marker, uint64_t frameTokenHandle);

/// Get the Windows message ID used for PCL ping.
/// The application must handle this message in its message pump.
/// @param outStatsWindowMessage  Output: Windows message ID (WM_USER+xxx)
SL_BRIDGE_API int slBridge_PCLGetState(uint32_t* outStatsWindowMessage);


// ════════════════════════════════════════════════════════════════════════
// Phase 3: Vulkan Swapchain
// ════════════════════════════════════════════════════════════════════════

/// Create a Vulkan swapchain and upgrade it via slUpgradeInterface for SL hooking.
SL_BRIDGE_API int slBridge_CreateSwapchain(
    uint64_t vkInstance,
    uint64_t vkPhysicalDevice,
    uint64_t vkDevice,
    uint64_t vkSurfaceKHR,
    uint32_t width,
    uint32_t height,
    uint32_t imageFormat,
    uint32_t presentQueueFamily
);

/// Destroy the SL-hooked swapchain.
SL_BRIDGE_API int slBridge_DestroySwapchain();

/// Acquire the next swapchain image.
/// @param imageAvailableSemaphore  VkSemaphore signaled when image is ready
/// @param outImageIndex  Output: index of acquired image
SL_BRIDGE_API int slBridge_AcquireNextImage(
    uint64_t imageAvailableSemaphore,
    uint32_t* outImageIndex
);

/// Present the swapchain image. SL intercepts this for DLSS-G frame injection.
/// @param imageIndex   Image index from AcquireNextImage
/// @param waitSemaphores  Array of VkSemaphores to wait on before presenting
/// @param numWaitSemaphores  Number of semaphores
SL_BRIDGE_API int slBridge_Present(
    uint32_t imageIndex,
    uint64_t* waitSemaphores,
    uint32_t numWaitSemaphores
);

/// Recreate the swapchain (e.g. on resize or DLSS-G toggle).
SL_BRIDGE_API int slBridge_RecreateSwapchain(uint32_t width, uint32_t height);

/// Get handles to swapchain images for blit targets.
/// @param outImages  Output array of VkImage handles
/// @param outCount   Input: array capacity; Output: actual count
SL_BRIDGE_API int slBridge_GetSwapchainImages(uint64_t* outImages, uint32_t* outCount);


// ════════════════════════════════════════════════════════════════════════
// Phase 4: Common Constants + Resource Tagging
// ════════════════════════════════════════════════════════════════════════

/// Set per-frame camera constants. All matrices MUST be row-major.
/// Call as early in the frame as possible after slBridge_GetNewFrameToken.
///
/// Matrix convention:
///   SL expects row-major. JOML/Minecraft uses column-major.
///   The Java layer must transpose before calling this function.
///
/// mvecScale convention:
///   If motion vectors are in pixel space:   {1.0/renderW, 1.0/renderH}
///   If motion vectors are in [-1,1] range:  {1.0, 1.0}
SL_BRIDGE_API int slBridge_SetConstants(
    uint64_t frameTokenHandle,
    uint32_t viewportId,
    const float* clipToCameraView,    // float[16], row-major
    const float* cameraViewToClip,    // float[16], row-major
    const float* worldToCameraView,   // float[16], row-major
    const float* cameraViewToWorld,   // float[16], row-major
    const float* prevClipToCameraView,// float[16], row-major
    const float* prevCameraViewToClip,// float[16], row-major
    float jitterOffsetX,
    float jitterOffsetY,
    float mvecScaleX,
    float mvecScaleY,
    float cameraNear,
    float cameraFar,
    float cameraFOV,
    float aspectRatio,
    uint32_t renderWidth,
    uint32_t renderHeight,
    int reset
);

/// Tag GPU resources for a frame. Pass 0/NULL for unused resources.
/// Resources are identified by their Vulkan handles.
SL_BRIDGE_API int slBridge_TagResources(
    uint64_t frameTokenHandle,
    uint32_t viewportId,
    uint64_t cmdBuffer,
    // Depth buffer (DLSS-G, DLSS SR, DLSS-RR)
    uint64_t depthImage, uint64_t depthView,
    uint32_t depthW, uint32_t depthH, uint32_t depthFmt,
    // Motion vectors (DLSS-G, DLSS SR, DLSS-RR)
    uint64_t mvecImage, uint64_t mvecView,
    uint32_t mvecW, uint32_t mvecH, uint32_t mvecFmt,
    // HUDless color (DLSS-G)
    uint64_t hudlessImage, uint64_t hudlessView,
    uint32_t hudlessW, uint32_t hudlessH, uint32_t hudlessFmt,
    // UI color + alpha (DLSS-G)
    uint64_t uiImage, uint64_t uiView,
    uint32_t uiW, uint32_t uiH, uint32_t uiFmt,
    // Exposure (DLSS SR, optional DLSS-G)
    uint64_t exposureImage, uint64_t exposureView,
    uint32_t exposureW, uint32_t exposureH, uint32_t exposureFmt,
    // Scaling input color (future: DLSS SR, RR, NIS, DeepDVC)
    uint64_t colorInImage, uint64_t colorInView,
    uint32_t colorInW, uint32_t colorInH, uint32_t colorInFmt,
    // Scaling output color (future: DLSS SR, RR, NIS, DeepDVC)
    uint64_t colorOutImage, uint64_t colorOutView,
    uint32_t colorOutW, uint32_t colorOutH, uint32_t colorOutFmt
);


// ════════════════════════════════════════════════════════════════════════
// Phase 4: DLSS-G (Frame Generation)
// ════════════════════════════════════════════════════════════════════════

/// Set DLSS-G options.
/// @param mode  0=Off, 1=On, 2=Auto
/// @param numFramesToGenerate  1 for 2x, 2 for 3x, 3 for 4x
SL_BRIDGE_API int slBridge_DLSSGSetOptions(
    uint32_t viewportId,
    int mode,
    uint32_t numFramesToGenerate
);

/// Get DLSS-G state.
SL_BRIDGE_API int slBridge_DLSSGGetState(
    uint32_t viewportId,
    uint64_t* outEstimatedVRAM,
    uint32_t* outStatus,
    uint32_t* outMinWidthOrHeight,
    uint32_t* outNumFramesToGenerateMax
);


// ════════════════════════════════════════════════════════════════════════
// Future: Feature Evaluation (DLSS SR, DLSS-RR, NIS, DeepDVC)
// ════════════════════════════════════════════════════════════════════════

/// Evaluate a feature (dispatch GPU work).
/// Used for DLSS SR, DLSS-RR, NIS, DeepDVC — not for Reflex/PCL.
SL_BRIDGE_API int slBridge_EvaluateFeature(
    uint32_t featureId,
    uint64_t frameTokenHandle,
    uint32_t viewportId,
    uint64_t cmdBuffer
);

/// Explicitly allocate resources for a feature + viewport.
SL_BRIDGE_API int slBridge_AllocateResources(
    uint32_t featureId,
    uint32_t viewportId,
    uint64_t cmdBuffer
);

/// Free resources for a feature + viewport.
SL_BRIDGE_API int slBridge_FreeResources(
    uint32_t featureId,
    uint32_t viewportId
);


// ════════════════════════════════════════════════════════════════════════
// Future: DLSS SR via Streamline
// ════════════════════════════════════════════════════════════════════════

SL_BRIDGE_API int slBridge_DLSSSetOptions(
    uint32_t viewportId,
    int mode,
    uint32_t outputWidth,
    uint32_t outputHeight,
    float sharpness,
    int colorBuffersHDR,
    int useAutoExposure
);

SL_BRIDGE_API int slBridge_DLSSGetOptimalSettings(
    int mode,
    uint32_t outputWidth,
    uint32_t outputHeight,
    uint32_t* outRenderWidth,
    uint32_t* outRenderHeight,
    float* outSharpness
);

SL_BRIDGE_API int slBridge_DLSSGetState(
    uint32_t viewportId,
    uint64_t* outEstimatedVRAM
);


// ════════════════════════════════════════════════════════════════════════
// Future: DLSS-RR Additional Resource Tagging
// ════════════════════════════════════════════════════════════════════════

SL_BRIDGE_API int slBridge_TagDLSSRRResources(
    uint64_t frameTokenHandle,
    uint32_t viewportId,
    uint64_t cmdBuffer,
    // Diffuse albedo
    uint64_t albedoImage, uint64_t albedoView,
    uint32_t albedoW, uint32_t albedoH, uint32_t albedoFmt,
    // Specular albedo
    uint64_t specAlbedoImage, uint64_t specAlbedoView,
    uint32_t specAlbedoW, uint32_t specAlbedoH, uint32_t specAlbedoFmt,
    // Normals + roughness (packed)
    uint64_t normalRoughImage, uint64_t normalRoughView,
    uint32_t normalRoughW, uint32_t normalRoughH, uint32_t normalRoughFmt,
    // Specular motion vectors
    uint64_t specMvecImage, uint64_t specMvecView,
    uint32_t specMvecW, uint32_t specMvecH, uint32_t specMvecFmt
);

#ifdef __cplusplus
}
#endif

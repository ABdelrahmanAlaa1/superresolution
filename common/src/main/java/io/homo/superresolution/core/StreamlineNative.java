/*
 * Super Resolution
 * Copyright (c) 2025-2026. 187J3X1-114514
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

package io.homo.superresolution.core;

/**
 * JNI declarations for the SRNativeStreamline native module.
 * Maps 1:1 to the slBridge_* C API in streamline_api.h.
 *
 * <p>All VkImage/VkDevice/VkInstance handles are passed as {@code long}.
 * All matrices are float[16] in <b>row-major</b> order (Java side must transpose from JOML column-major).
 */
public class StreamlineNative {

    // ════════════════════════════════════════════════════════════════
    // Phase 1: Core Lifecycle
    // ════════════════════════════════════════════════════════════════

    /**
     * Initialize Streamline SDK with manual hooking + frame-based tagging.
     * @param pluginsPath Absolute path to the streamline/ folder containing SL DLLs
     * @param appId       NVIDIA Application ID (0 = development mode)
     * @param logLevel    0=off, 1=default, 2=verbose
     * @return 0 on success
     */
    public static native int NslInit(String pluginsPath, long appId, int logLevel);

    /** Shutdown Streamline SDK. Must be called before destroying VkDevice. */
    public static native int NslShutdown();

    /**
     * Register the existing Vulkan device with Streamline.
     * Must be called after NslInit and after VkDevice creation.
     */
    public static native int NslSetVulkanInfo(long instance, long physDev, long device,
                                               int gfxQueueFamily, int gfxQueueIdx);

    /**
     * Check if a specific SL feature is supported on this system.
     * Feature IDs: 0=DLSS, 1=Reflex, 2=NIS, 3=DLSS_G, 5=DeepDVC, 1001=DLSS_RR
     */
    public static native boolean NslIsFeatureSupported(int featureId);

    /**
     * Obtain a new FrameToken for the given frame index.
     * @param frameIndex Monotonically increasing frame counter
     * @return Opaque token handle, or 0 on error
     */
    public static native long NslGetNewFrameToken(int frameIndex);


    // ════════════════════════════════════════════════════════════════
    // Phase 2: Reflex
    // ════════════════════════════════════════════════════════════════

    /**
     * Set Reflex mode and optional frame limiter.
     * @param mode        0=Off, 1=LowLatency, 2=LowLatencyWithBoost
     * @param frameLimitUs Frame limit in microseconds (0 = no limit)
     */
    public static native int NslReflexSetOptions(int mode, int frameLimitUs);

    /** Perform Reflex sleep. Must be called every frame regardless of mode. */
    public static native int NslReflexSleep(long frameToken);

    /**
     * Get current Reflex state/stats.
     * @return float[4]: [lowLatAvail (0 or 1), gpuActiveMs, gpuFrameMs, pcLatencyMs]
     */
    public static native float[] NslReflexGetState();

    /**
     * Provide camera data for Reflex camera prediction (improves DLSS-G quality).
     * Matrices must be row-major float[16].
     */
    public static native int NslReflexSetCameraData(long frameToken,
                                                     float[] viewToClip, float[] worldToView);


    // ════════════════════════════════════════════════════════════════
    // Phase 2: PCL (PC Latency) Markers
    // ════════════════════════════════════════════════════════════════

    // PCL Marker constants
    public static final int PCL_SIMULATION_START = 0;
    public static final int PCL_SIMULATION_END = 1;
    public static final int PCL_RENDER_SUBMIT_START = 2;
    public static final int PCL_RENDER_SUBMIT_END = 3;
    public static final int PCL_PRESENT_START = 4;
    public static final int PCL_PRESENT_END = 5;
    public static final int PCL_INPUT_SAMPLE = 6;
    public static final int PCL_TRIGGER_FLASH = 7;
    public static final int PCL_PC_LATENCY_PING = 8;

    /** Set a PCL marker. Must be called every frame at each marker point. */
    public static native int NslPCLSetMarker(int markerType, long frameToken);

    /**
     * Get the Windows message ID used for PCL ping.
     * @return Windows message ID, or 0 if unavailable
     */
    public static native int NslPCLGetStatsWindowMessage();


    // ════════════════════════════════════════════════════════════════
    // Phase 3: Vulkan Swapchain
    // ════════════════════════════════════════════════════════════════

    /** Create an SL-hooked Vulkan swapchain. */
    public static native int NslCreateSwapchain(long instance, long physDev, long device,
                                                 long surface, int w, int h, int format,
                                                 int presentQueueFamily);

    /** Destroy the SL-hooked swapchain. */
    public static native int NslDestroySwapchain();

    /**
     * Acquire the next swapchain image.
     * @param semaphore VkSemaphore signaled when image is ready
     * @return Image index, or -1 on error
     */
    public static native int NslAcquireNextImage(long semaphore);

    /**
     * Present the swapchain image. SL intercepts this for DLSS-G.
     * @param imageIndex    Image index from AcquireNextImage
     * @param waitSemaphores Array of VkSemaphores to wait on
     */
    public static native int NslPresent(int imageIndex, long[] waitSemaphores);

    /** Recreate the swapchain (e.g. on resize or DLSS-G toggle). */
    public static native int NslRecreateSwapchain(int w, int h);

    /** Get VkImage handles from the swapchain. */
    public static native long[] NslGetSwapchainImages();


    // ════════════════════════════════════════════════════════════════
    // Phase 4: Constants + Tagging + DLSS-G
    // ════════════════════════════════════════════════════════════════

    /**
     * Set per-frame camera constants. All matrices MUST be row-major float[16].
     * Call as early in the frame as possible after NslGetNewFrameToken.
     */
    public static native int NslSetConstants(long frameToken, int viewportId,
                                              float[] clipToCamView, float[] camViewToClip,
                                              float[] worldToCamView, float[] camViewToWorld,
                                              float[] prevClipToCamView, float[] prevCamViewToClip,
                                              float jitterX, float jitterY,
                                              float mvecScaleX, float mvecScaleY,
                                              float cameraNear, float cameraFar,
                                              float cameraFOV, float aspectRatio,
                                              int renderW, int renderH, boolean reset);

    /**
     * Tag GPU resources for a frame. Pass 0 for unused resources.
     * Resources: depth, mvec, HUDless color, UI color+alpha, exposure, scaling I/O.
     */
    public static native int NslTagResources(long frameToken, int viewportId, long cmdBuf,
                                              long depthImg, long depthView, int dW, int dH, int dFmt,
                                              long mvecImg, long mvecView, int mW, int mH, int mFmt,
                                              long hudlessImg, long hudlessView, int hW, int hH, int hFmt,
                                              long uiImg, long uiView, int uW, int uH, int uFmt,
                                              long exposureImg, long exposureView, int eW, int eH, int eFmt,
                                              long colorInImg, long colorInView, int ciW, int ciH, int ciFmt,
                                              long colorOutImg, long colorOutView, int coW, int coH, int coFmt);

    /**
     * Set DLSS-G options.
     * @param mode                0=Off, 1=On, 2=Auto
     * @param numFrames           1 for 2x, 2 for 3x, 3 for 4x
     */
    public static native int NslDLSSGSetOptions(int viewportId, int mode, int numFrames);

    /**
     * Get DLSS-G state.
     * @return long[4]: [estimatedVRAM, status, minWidthOrHeight, numFramesToGenerateMax]
     */
    public static native long[] NslDLSSGGetState(int viewportId);


    // ════════════════════════════════════════════════════════════════
    // Future: Feature Evaluation + Resource Lifecycle
    // ════════════════════════════════════════════════════════════════

    /** Evaluate a feature (dispatch GPU work). For DLSS SR, DLSS-RR, NIS, DeepDVC. */
    public static native int NslEvaluateFeature(int featureId, long frameToken,
                                                 int viewportId, long cmdBuf);

    /** Explicitly allocate GPU resources for a feature + viewport. */
    public static native int NslAllocateResources(int featureId, int viewportId, long cmdBuf);

    /** Free GPU resources for a feature + viewport. */
    public static native int NslFreeResources(int featureId, int viewportId);


    // ════════════════════════════════════════════════════════════════
    // Feature ID constants (match sl_core_types.h)
    // ════════════════════════════════════════════════════════════════

    public static final int FEATURE_DLSS = 0;
    public static final int FEATURE_REFLEX = 1;
    public static final int FEATURE_NIS = 2;
    public static final int FEATURE_DLSS_G = 3;
    public static final int FEATURE_DEEP_DVC = 5;
    public static final int FEATURE_DLSS_RR = 1001;
}

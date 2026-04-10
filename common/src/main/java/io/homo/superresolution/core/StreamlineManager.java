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

// StreamlineConfig is in the same package (core)
import io.homo.superresolution.core.RenderSystems;
import io.homo.superresolution.core.graphics.vulkan.VkRenderSystem;
import io.homo.superresolution.core.graphics.vulkan.VulkanDevice;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * High-level orchestrator for Streamline features (Reflex, PCL, DLSS-G).
 * Called from the render loop via {@link io.homo.superresolution.common.minecraft.handler.RenderHandlerManager}.
 *
 * <h3>Per-frame call sequence:</h3>
 * <pre>
 * onFrameBegin() → beginFrame(frameCount)
 *   ├─ slGetNewFrameToken
 *   ├─ slPCLSetMarker(SimulationStart)
 *   └─ slReflexSleep
 *
 * [simulation runs]
 *
 * endSimulation()
 *   └─ slPCLSetMarker(SimulationEnd)
 *
 * beginRenderSubmit()
 *   ├─ slPCLSetMarker(RenderSubmitStart)
 *   ├─ slSetConstants(camera matrices)
 *   └─ slSetTagForFrame(resources)
 *
 * [VK command submit]
 *
 * endRenderSubmit()
 *   └─ slPCLSetMarker(RenderSubmitEnd)
 *
 * beforePresent()
 *   └─ slPCLSetMarker(PresentStart)
 *
 * [VulkanPresenter.present()]
 *
 * afterPresent()
 *   └─ slPCLSetMarker(PresentEnd)
 * </pre>
 */
public class StreamlineManager {

    private static final Logger LOGGER = LoggerFactory.getLogger("StreamlineManager");

    // ── Singleton ──
    private static StreamlineManager INSTANCE;

    // ── State ──
    private boolean initialized = false;
    private boolean reflexSupported = false;
    private boolean pclSupported = false;
    private boolean dlssgSupported = false;

    private ReflexMode reflexMode = ReflexMode.OFF;
    private boolean dlssgEnabled = false;
    private int dlssgMultiplier = 1; // 1=2x, 2=3x, 3=4x

    // ── Per-frame ──
    private long currentFrameToken = 0;
    private int currentFrameIndex = 0;

    // ══════════════════════════════════════════════════════════════════
    // Enums
    // ══════════════════════════════════════════════════════════════════

    public enum ReflexMode {
        OFF(0),
        ON(1),
        ON_BOOST(2);

        public final int value;
        ReflexMode(int v) { this.value = v; }
    }

    // ══════════════════════════════════════════════════════════════════
    // Lifecycle
    // ══════════════════════════════════════════════════════════════════

    public static StreamlineManager getInstance() {
        return INSTANCE;
    }

    /**
     * Initialize Streamline. Called once during mod startup, after VkDevice creation.
     *
     * @param pluginsPath Absolute path to the streamline/ folder
     * @param config      Persistent configuration
     */
    public static StreamlineManager initialize(String pluginsPath, StreamlineConfig config) {
        if (INSTANCE != null && INSTANCE.initialized) {
            LOGGER.warn("StreamlineManager already initialized");
            return INSTANCE;
        }

        INSTANCE = new StreamlineManager();
        INSTANCE.doInit(pluginsPath, config);
        return INSTANCE;
    }

    private void doInit(String pluginsPath, StreamlineConfig config) {
        LOGGER.info("Initializing Streamline SDK...");

        // 1. slInit
        int res = StreamlineNative.NslInit(pluginsPath, 0, config.debugLogging ? 2 : 1);
        if (res != 0) {
            LOGGER.error("slInit failed with error code: {}", res);
            return;
        }

        // 2. Register Vulkan device
        VkRenderSystem vkrs = RenderSystems.vulkan();
        if (vkrs == null) {
            LOGGER.error("Vulkan not available, cannot initialize Streamline");
            StreamlineNative.NslShutdown();
            return;
        }
        VulkanDevice device = vkrs.device();
        res = StreamlineNative.NslSetVulkanInfo(
                vkrs.getVulkanInstance().address(),
                device.getPhysicalDevice().address(),
                device.getVkDevice().address(),
                device.getMainQueue().getQueueFamilyIndex(),
                0 // queue index
        );
        if (res != 0) {
            LOGGER.error("slSetVulkanInfo failed with error code: {}", res);
            StreamlineNative.NslShutdown();
            return;
        }

        // 3. Query feature support
        reflexSupported = StreamlineNative.NslIsFeatureSupported(StreamlineNative.FEATURE_REFLEX);
        pclSupported = StreamlineNative.NslIsFeatureSupported(StreamlineNative.FEATURE_DLSS_G);
        dlssgSupported = StreamlineNative.NslIsFeatureSupported(StreamlineNative.FEATURE_DLSS_G);

        LOGGER.info("Feature support — Reflex: {}, PCL: {}, DLSS-G: {}",
                reflexSupported, pclSupported, dlssgSupported);

        // 4. Apply initial config
        if (reflexSupported) {
            setReflexMode(config.reflexMode);
        }

        initialized = true;
        LOGGER.info("Streamline SDK initialized successfully");
    }

    public void shutdown() {
        if (!initialized) return;

        LOGGER.info("Shutting down Streamline SDK...");
        if (dlssgEnabled) {
            StreamlineNative.NslDLSSGSetOptions(0, 0, 0);
        }
        StreamlineNative.NslShutdown();
        initialized = false;
        INSTANCE = null;
    }

    public boolean isInitialized() {
        return initialized;
    }


    // ══════════════════════════════════════════════════════════════════
    // Per-Frame Calls (from RenderHandlerManager)
    // ══════════════════════════════════════════════════════════════════

    /**
     * Called at the TOP of the frame, from {@code RenderHandlerManager.onFrameBegin()}.
     * Obtains a new FrameToken, sets SimulationStart marker, and calls Reflex sleep.
     */
    public void beginFrame(int frameIndex) {
        if (!initialized) return;

        currentFrameIndex = frameIndex;
        currentFrameToken = StreamlineNative.NslGetNewFrameToken(frameIndex);
        if (currentFrameToken == 0) return;

        // PCL: Simulation start
        StreamlineNative.NslPCLSetMarker(StreamlineNative.PCL_SIMULATION_START, currentFrameToken);

        // Reflex sleep — must be called every frame
        if (reflexSupported) {
            StreamlineNative.NslReflexSleep(currentFrameToken);
        }
    }

    /** Called after game simulation, before rendering. */
    public void endSimulation() {
        if (!initialized || currentFrameToken == 0) return;
        StreamlineNative.NslPCLSetMarker(StreamlineNative.PCL_SIMULATION_END, currentFrameToken);
    }

    /** Called before Vulkan command buffer recording. */
    public void beginRenderSubmit() {
        if (!initialized || currentFrameToken == 0) return;
        StreamlineNative.NslPCLSetMarker(StreamlineNative.PCL_RENDER_SUBMIT_START, currentFrameToken);
    }

    /** Called after Vulkan command buffer submit. */
    public void endRenderSubmit() {
        if (!initialized || currentFrameToken == 0) return;
        StreamlineNative.NslPCLSetMarker(StreamlineNative.PCL_RENDER_SUBMIT_END, currentFrameToken);
    }

    /** Called before present. */
    public void beforePresent() {
        if (!initialized || currentFrameToken == 0) return;
        StreamlineNative.NslPCLSetMarker(StreamlineNative.PCL_PRESENT_START, currentFrameToken);
    }

    /** Called after present. */
    public void afterPresent() {
        if (!initialized || currentFrameToken == 0) return;
        StreamlineNative.NslPCLSetMarker(StreamlineNative.PCL_PRESENT_END, currentFrameToken);
    }

    /** Handle PCL ping (called from Windows message pump). */
    public void handlePCLPing() {
        if (!initialized || currentFrameToken == 0) return;
        StreamlineNative.NslPCLSetMarker(StreamlineNative.PCL_PC_LATENCY_PING, currentFrameToken);
    }


    // ══════════════════════════════════════════════════════════════════
    // Configuration
    // ══════════════════════════════════════════════════════════════════

    public void setReflexMode(ReflexMode mode) {
        // DLSS-G requires Reflex to be at least ON
        if (dlssgEnabled && mode == ReflexMode.OFF) {
            mode = ReflexMode.ON;
        }
        this.reflexMode = mode;
        if (initialized && reflexSupported) {
            StreamlineNative.NslReflexSetOptions(mode.value, 0);
        }
    }

    public ReflexMode getReflexMode() {
        return reflexMode;
    }

    public void setDLSSGEnabled(boolean enabled) {
        if (!initialized || !dlssgSupported) return;

        if (enabled) {
            // Enforce Reflex >= ON
            if (reflexMode == ReflexMode.OFF) {
                setReflexMode(ReflexMode.ON);
            }
        }

        this.dlssgEnabled = enabled;
        StreamlineNative.NslDLSSGSetOptions(0,
                enabled ? 1 : 0,
                dlssgMultiplier);

        LOGGER.info("DLSS-G {}", enabled ? "enabled" : "disabled");
    }

    public boolean isDlssgEnabled() {
        return dlssgEnabled;
    }

    public void setDlssgMultiplier(int multiplier) {
        this.dlssgMultiplier = Math.max(1, Math.min(3, multiplier));
        if (dlssgEnabled) {
            StreamlineNative.NslDLSSGSetOptions(0, 1, this.dlssgMultiplier);
        }
    }

    /**
     * Called when world state changes (join/leave).
     * DLSS-G must be disabled during loading screens and transitions.
     */
    public void onWorldStateChange(boolean isInWorld) {
        if (!initialized || !dlssgSupported) return;

        if (!isInWorld && dlssgEnabled) {
            // Temporarily disable FG during transition
            StreamlineNative.NslDLSSGSetOptions(0, 0, 0);
            LOGGER.info("DLSS-G temporarily disabled (world transition)");
        } else if (isInWorld && dlssgEnabled) {
            // Restore FG
            StreamlineNative.NslDLSSGSetOptions(0, 1, dlssgMultiplier);
            LOGGER.info("DLSS-G restored (world loaded)");
        }
    }


    // ══════════════════════════════════════════════════════════════════
    // State Queries
    // ══════════════════════════════════════════════════════════════════

    /**
     * Get Reflex latency stats.
     * @return float[4]: [lowLatAvail, gpuActiveMs, gpuFrameMs, pcLatencyMs], or null
     */
    public float[] getReflexState() {
        if (!initialized || !reflexSupported) return null;
        return StreamlineNative.NslReflexGetState();
    }

    public boolean isReflexSupported() { return reflexSupported; }
    public boolean isDlssgSupported() { return dlssgSupported; }

    public long getCurrentFrameToken() { return currentFrameToken; }
    public int getCurrentFrameIndex() { return currentFrameIndex; }


    // ══════════════════════════════════════════════════════════════════
    // Camera Data (for Reflex prediction + slSetConstants)
    // ══════════════════════════════════════════════════════════════════

    /**
     * Set per-frame constants. Called from render submit phase.
     */
    public void setFrameConstants(float[] clipToCamView, float[] camViewToClip,
                                   float[] worldToCamView, float[] camViewToWorld,
                                   float[] prevClipToCamView, float[] prevCamViewToClip,
                                   float jitterX, float jitterY,
                                   float mvecScaleX, float mvecScaleY,
                                   float cameraNear, float cameraFar,
                                   float cameraFOV, float aspectRatio,
                                   int renderW, int renderH, boolean reset) {
        if (!initialized || currentFrameToken == 0) return;

        StreamlineNative.NslSetConstants(
                currentFrameToken, 0,
                clipToCamView, camViewToClip,
                worldToCamView, camViewToWorld,
                prevClipToCamView, prevCamViewToClip,
                jitterX, jitterY,
                mvecScaleX, mvecScaleY,
                cameraNear, cameraFar,
                cameraFOV, aspectRatio,
                renderW, renderH, reset
        );

        // Also feed camera data for Reflex prediction
        if (reflexSupported) {
            StreamlineNative.NslReflexSetCameraData(
                    currentFrameToken, camViewToClip, worldToCamView);
        }
    }
}

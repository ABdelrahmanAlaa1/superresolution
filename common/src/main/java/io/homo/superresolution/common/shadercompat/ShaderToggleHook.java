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

package io.homo.superresolution.common.shadercompat;

import io.homo.superresolution.core.StreamlineManager;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Automatically toggles Iris shaders during world join to prevent crashes.
 *
 * <p>Uses <b>reflection-based soft dependency</b> on Iris API, following the same
 * pattern as the existing {@code ShaderCompatHandler}. This ensures no compile-time
 * dependency on Iris.</p>
 *
 * <h3>Lifecycle:</h3>
 * <ol>
 *   <li>Player joins world → {@link #onWorldJoinStart()} disables shaders + DLSS-G</li>
 *   <li>World finishes loading (after N frames) → {@link #onWorldJoinComplete()} restores shaders + DLSS-G</li>
 * </ol>
 */
public class ShaderToggleHook {

    private static final Logger LOGGER = LoggerFactory.getLogger("ShaderToggleHook");

    /** How many frames to wait after world load before re-enabling shaders */
    private static final int RESTORE_DELAY_FRAMES = 3;

    private static boolean wasEnabledBeforeJoin = false;
    private static int restoreCountdown = -1;

    /**
     * Called when the player starts joining a world.
     * Disables Iris shaders and DLSS-G to prevent crashes during transition.
     */
    public static void onWorldJoinStart() {
        try {
            Class<?> irisApi = Class.forName("net.irisshaders.iris.api.v0.IrisApi");
            Object instance = irisApi.getMethod("getInstance").invoke(null);
            Object config = irisApi.getMethod("getConfig").invoke(instance);

            boolean enabled = (boolean) config.getClass()
                    .getMethod("areShadersEnabled").invoke(config);

            if (enabled) {
                wasEnabledBeforeJoin = true;

                // Disable shaders
                config.getClass()
                        .getMethod("setShadersEnabledAndApply", boolean.class)
                        .invoke(config, false);
                LOGGER.info("Disabled Iris shaders for world join");

                // Disable DLSS-G during transition
                StreamlineManager slm = StreamlineManager.getInstance();
                if (slm != null && slm.isInitialized()) {
                    slm.onWorldStateChange(false);
                }
            }
        } catch (ClassNotFoundException e) {
            // Iris not installed — nothing to do
        } catch (Exception e) {
            LOGGER.warn("Failed to toggle Iris shaders: {}", e.getMessage());
        }
    }

    /**
     * Called when the world has finished loading.
     * Starts a countdown to re-enable shaders after a few frames.
     */
    public static void onWorldLoadComplete() {
        if (wasEnabledBeforeJoin) {
            restoreCountdown = RESTORE_DELAY_FRAMES;
            LOGGER.info("World loaded, will restore shaders in {} frames", RESTORE_DELAY_FRAMES);
        }
    }

    /**
     * Called every frame to tick the restore countdown.
     * Should be called from the render loop.
     */
    public static void tick() {
        if (restoreCountdown < 0) return;

        restoreCountdown--;
        if (restoreCountdown <= 0) {
            restoreCountdown = -1;
            onWorldJoinComplete();
        }
    }

    /**
     * Re-enables shaders and DLSS-G after the delay.
     */
    private static void onWorldJoinComplete() {
        if (!wasEnabledBeforeJoin) return;
        wasEnabledBeforeJoin = false;

        try {
            Class<?> irisApi = Class.forName("net.irisshaders.iris.api.v0.IrisApi");
            Object instance = irisApi.getMethod("getInstance").invoke(null);
            Object config = irisApi.getMethod("getConfig").invoke(instance);

            config.getClass()
                    .getMethod("setShadersEnabledAndApply", boolean.class)
                    .invoke(config, true);
            LOGGER.info("Restored Iris shaders after world join");
        } catch (Exception e) {
            LOGGER.warn("Failed to restore Iris shaders: {}", e.getMessage());
        }

        // Restore DLSS-G
        StreamlineManager slm = StreamlineManager.getInstance();
        if (slm != null && slm.isInitialized()) {
            slm.onWorldStateChange(true);
        }
    }

    /**
     * Force reset state (e.g. on disconnect).
     */
    public static void reset() {
        wasEnabledBeforeJoin = false;
        restoreCountdown = -1;
    }
}

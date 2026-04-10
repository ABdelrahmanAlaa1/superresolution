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
 * Persistent configuration for Streamline features.
 * Serialized to/from the mod's settings file.
 */
public class StreamlineConfig {

    /** Reflex mode: OFF, ON, ON_BOOST */
    public StreamlineManager.ReflexMode reflexMode = StreamlineManager.ReflexMode.OFF;

    /** Reflex frame limit in FPS (0 = no limit) */
    public int reflexFrameLimitFps = 0;

    /** Whether DLSS Frame Generation is enabled */
    public boolean dlssgEnabled = false;

    /**
     * DLSS-G frame generation multiplier.
     * 1 = 2x (generate 1 frame), 2 = 3x, 3 = 4x.
     */
    public int dlssgMultiplier = 1;

    /** Enable verbose Streamline logging (for development) */
    public boolean debugLogging = false;

    /**
     * Get the effective Reflex mode considering DLSS-G constraints.
     * DLSS-G requires Reflex to be at least ON.
     */
    public StreamlineManager.ReflexMode getEffectiveReflexMode() {
        if (dlssgEnabled && reflexMode == StreamlineManager.ReflexMode.OFF) {
            return StreamlineManager.ReflexMode.ON;
        }
        return reflexMode;
    }

    /**
     * Validate and clamp values to valid ranges.
     */
    public void validate() {
        if (reflexMode == null) reflexMode = StreamlineManager.ReflexMode.OFF;
        reflexFrameLimitFps = Math.max(0, reflexFrameLimitFps);
        dlssgMultiplier = Math.max(1, Math.min(3, dlssgMultiplier));
    }
}

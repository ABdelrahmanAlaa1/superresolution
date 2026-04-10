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

import org.joml.Matrix4f;

/**
 * Extracts camera matrices from Minecraft's rendering pipeline and converts
 * them to the row-major format required by Streamline.
 *
 * <h3>Matrix convention:</h3>
 * <ul>
 *   <li>JOML/Minecraft: <b>column-major</b> (memory layout: col0, col1, col2, col3)</li>
 *   <li>Streamline SL:  <b>row-major</b> (memory layout: row0, row1, row2, row3)</li>
 * </ul>
 * <p>Therefore we must <b>transpose</b> every matrix before passing to {@code slSetConstants}.</p>
 *
 * <h3>Additional SL requirements:</h3>
 * <ul>
 *   <li>Matrices must NOT contain jitter offsets</li>
 *   <li>Jitter is provided separately via jitterOffsetX/Y in pixel space</li>
 * </ul>
 */
public class StreamlineCameraHelper {

    // Previous frame matrices for temporal data
    private final float[] prevClipToCamView = new float[16];
    private final float[] prevCamViewToClip = new float[16];
    private boolean hasPreviousFrame = false;

    // Reusable buffers
    private final float[] tempMatrix = new float[16];

    /**
     * Convert a JOML column-major Matrix4f to a row-major float[16] for Streamline.
     * This is equivalent to transposing the matrix.
     *
     * @param m Input matrix (JOML column-major)
     * @param out Output float[16] in row-major order
     */
    public static void toRowMajor(Matrix4f m, float[] out) {
        // JOML get() returns column-major:
        //   [m00, m10, m20, m30, m01, m11, m21, m31, m02, m12, m22, m32, m03, m13, m23, m33]
        // We need row-major:
        //   [m00, m01, m02, m03, m10, m11, m12, m13, m20, m21, m22, m23, m30, m31, m32, m33]

        out[0]  = m.m00(); out[1]  = m.m01(); out[2]  = m.m02(); out[3]  = m.m03();
        out[4]  = m.m10(); out[5]  = m.m11(); out[6]  = m.m12(); out[7]  = m.m13();
        out[8]  = m.m20(); out[9]  = m.m21(); out[10] = m.m22(); out[11] = m.m23();
        out[12] = m.m30(); out[13] = m.m31(); out[14] = m.m32(); out[15] = m.m33();
    }

    /**
     * Create a row-major float[16] from a JOML Matrix4f. Allocates a new array.
     */
    public static float[] toRowMajor(Matrix4f m) {
        float[] out = new float[16];
        toRowMajor(m, out);
        return out;
    }

    /**
     * Update Streamline constants for this frame.
     * Call once per frame during the render submit phase.
     *
     * @param projection       Current projection matrix (no jitter!)
     * @param view             Current view matrix (camera → world inverse)
     * @param jitterX          Jitter offset X in pixel space
     * @param jitterY          Jitter offset Y in pixel space
     * @param mvecScaleX       Motion vector scale X (1/renderW for pixel-space mvecs)
     * @param mvecScaleY       Motion vector scale Y (1/renderH for pixel-space mvecs)
     * @param cameraNear       Near plane distance
     * @param cameraFar        Far plane distance
     * @param cameraFOV        Vertical FOV in radians
     * @param renderW          Render resolution width
     * @param renderH          Render resolution height
     * @param reset            True on scene cuts, teleports, world joins
     */
    public void updateConstants(Matrix4f projection, Matrix4f view,
                                 float jitterX, float jitterY,
                                 float mvecScaleX, float mvecScaleY,
                                 float cameraNear, float cameraFar, float cameraFOV,
                                 int renderW, int renderH, boolean reset) {
        StreamlineManager slm = StreamlineManager.getInstance();
        if (slm == null || !slm.isInitialized()) return;

        // Compute required matrices
        Matrix4f invProjection = new Matrix4f(projection).invert();   // clipToCameraView
        Matrix4f invView = new Matrix4f(view).invert();               // cameraViewToWorld

        // Convert to row-major
        float[] clipToCamView = toRowMajor(invProjection);
        float[] camViewToClip = toRowMajor(projection);
        float[] worldToCamView = toRowMajor(view);
        float[] camViewToWorld = toRowMajor(invView);

        // Previous frame (for temporal stability)
        float[] prevClipToCam, prevCamViewToClp;
        if (hasPreviousFrame && !reset) {
            prevClipToCam = prevClipToCamView.clone();
            prevCamViewToClp = prevCamViewToClip.clone();
        } else {
            // No previous frame — use current (SL handles this gracefully)
            prevClipToCam = clipToCamView.clone();
            prevCamViewToClp = camViewToClip.clone();
        }

        // Store for next frame
        System.arraycopy(clipToCamView, 0, prevClipToCamView, 0, 16);
        System.arraycopy(camViewToClip, 0, prevCamViewToClip, 0, 16);
        hasPreviousFrame = true;

        float aspectRatio = (float) renderW / (float) renderH;

        slm.setFrameConstants(
                clipToCamView, camViewToClip,
                worldToCamView, camViewToWorld,
                prevClipToCam, prevCamViewToClp,
                jitterX, jitterY,
                mvecScaleX, mvecScaleY,
                cameraNear, cameraFar,
                cameraFOV, aspectRatio,
                renderW, renderH, reset
        );
    }

    /**
     * Reset temporal state. Call on scene cuts, world changes, etc.
     */
    public void reset() {
        hasPreviousFrame = false;
    }
}

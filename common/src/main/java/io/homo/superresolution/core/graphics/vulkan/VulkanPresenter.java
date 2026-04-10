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

package io.homo.superresolution.core.graphics.vulkan;

import io.homo.superresolution.common.minecraft.MinecraftWindow;
import io.homo.superresolution.core.RenderSystems;
import io.homo.superresolution.core.StreamlineManager;
import io.homo.superresolution.core.StreamlineNative;
import org.lwjgl.glfw.GLFWNativeWin32;
import org.lwjgl.system.MemoryStack;
import org.lwjgl.vulkan.*;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import static io.homo.superresolution.core.graphics.vulkan.utils.VulkanUtils.VK_CHECK;
import static org.lwjgl.glfw.GLFW.*;
import static org.lwjgl.opengl.EXTSemaphore.*;
import static org.lwjgl.opengl.GL11.*;
import static org.lwjgl.opengl.GL30.*;
import static org.lwjgl.vulkan.KHRSurface.*;
import static org.lwjgl.vulkan.KHRWin32Surface.*;
import static org.lwjgl.vulkan.VK10.*;

/**
 * Manages the GL→VK present path for DLSS Frame Generation.
 *
 * <h3>Flow per frame when active:</h3>
 * <ol>
 *   <li>GL renders to its default framebuffer as normal</li>
 *   <li>At present time, instead of {@code glfwSwapBuffers}:</li>
 *   <li>GL signals a semaphore (GL→VK sync)</li>
 *   <li>VK acquires next swapchain image</li>
 *   <li>VK blits GL framebuffer → swapchain image (via shared memory texture)</li>
 *   <li>VK calls {@code vkQueuePresentKHR} (intercepted by Streamline for DLSS-G)</li>
 * </ol>
 *
 * <p>When DLSS-G is <b>not active</b>, this presenter falls back to normal
 * {@code glfwSwapBuffers} to avoid overhead.</p>
 */
public class VulkanPresenter {

    private static final Logger LOGGER = LoggerFactory.getLogger("VulkanPresenter");

    // ── Singleton ──
    private static VulkanPresenter INSTANCE;

    // ── State ──
    private boolean initialized = false;
    private boolean active = false; // true = VK present path, false = GL present path

    // ── VK handles ──
    private long vkSurface = VK_NULL_HANDLE;

    // ── GL→VK synchronization ──
    private VkGlInteropSemaphore glToVkSemaphore;
    private VkGlInteropSemaphore vkToGlSemaphore;

    // ── Dimensions ──
    private int presentWidth;
    private int presentHeight;

    // ══════════════════════════════════════════════════════════════════
    // Lifecycle
    // ══════════════════════════════════════════════════════════════════

    public static VulkanPresenter getInstance() {
        return INSTANCE;
    }

    /**
     * Initialize the Vulkan presenter. Call after StreamlineManager.initialize().
     */
    public static VulkanPresenter initialize() {
        if (INSTANCE != null && INSTANCE.initialized) {
            return INSTANCE;
        }
        INSTANCE = new VulkanPresenter();
        INSTANCE.doInit();
        return INSTANCE;
    }

    private void doInit() {
        VkRenderSystem vkrs = RenderSystems.vulkan();
        if (vkrs == null) {
            LOGGER.warn("Vulkan not available, VulkanPresenter disabled");
            return;
        }

        VulkanDevice device = vkrs.device();

        // Create GL↔VK sync semaphores
        glToVkSemaphore = VkGlInteropSemaphore.create(device);
        vkToGlSemaphore = VkGlInteropSemaphore.create(device);

        // Cache initial window size
        presentWidth = MinecraftWindow.getWindowWidth();
        presentHeight = MinecraftWindow.getWindowHeight();

        initialized = true;
        LOGGER.info("VulkanPresenter initialized ({}x{})", presentWidth, presentHeight);
    }

    /**
     * Create the VK surface and swapchain. Must be called from render thread.
     * Call this when transitioning from GL present to VK present (DLSS-G enable).
     */
    public boolean createSurface() {
        if (!initialized) return false;

        VkRenderSystem vkrs = RenderSystems.vulkan();
        VulkanDevice device = vkrs.device();
        long windowHandle = MinecraftWindow.getWindowHandle();

        if (windowHandle <= 0) {
            LOGGER.error("Invalid window handle");
            return false;
        }

        // Create Win32 surface from GLFW window
        try (MemoryStack stack = MemoryStack.stackPush()) {
            long hwnd = GLFWNativeWin32.glfwGetWin32Window(windowHandle);

            VkWin32SurfaceCreateInfoKHR surfaceInfo = VkWin32SurfaceCreateInfoKHR.calloc(stack)
                    .sType(VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR)
                    .hwnd(hwnd)
                    .hinstance(org.lwjgl.system.windows.User32.GetModuleHandle((java.nio.ByteBuffer) null));

            long[] pSurface = new long[1];
            VK_CHECK(vkCreateWin32SurfaceKHR(
                    vkrs.getVulkanInstance(), surfaceInfo, null, pSurface
            ), "Failed to create Win32 Vulkan surface");
            vkSurface = pSurface[0];

            LOGGER.info("VkSurfaceKHR created: 0x{}", Long.toHexString(vkSurface));
        }

        // Create SL-hooked swapchain via native bridge
        presentWidth = MinecraftWindow.getWindowWidth();
        presentHeight = MinecraftWindow.getWindowHeight();

        int res = StreamlineNative.NslCreateSwapchain(
                vkrs.getVulkanInstance().address(),
                device.getPhysicalDevice().address(),
                device.getVkDevice().address(),
                vkSurface,
                presentWidth, presentHeight,
                0, // auto-detect format
                device.getMainQueue().getQueueFamilyIndex()
        );

        if (res != 0) {
            LOGGER.error("Failed to create swapchain: {}", res);
            destroySurface();
            return false;
        }

        active = true;
        LOGGER.info("VulkanPresenter activated (VK present path)");
        return true;
    }

    /**
     * Destroy the surface and swapchain. Fall back to GL present.
     */
    public void destroySurface() {
        active = false;

        StreamlineNative.NslDestroySwapchain();

        if (vkSurface != VK_NULL_HANDLE) {
            VkRenderSystem vkrs = RenderSystems.vulkan();
            if (vkrs != null) {
                vkDestroySurfaceKHR(vkrs.getVulkanInstance(), vkSurface, null);
            }
            vkSurface = VK_NULL_HANDLE;
        }

        LOGGER.info("VulkanPresenter deactivated (GL present path)");
    }

    /**
     * Shutdown the presenter entirely.
     */
    public void shutdown() {
        if (!initialized) return;

        destroySurface();

        if (glToVkSemaphore != null) {
            glToVkSemaphore.destroy();
            glToVkSemaphore = null;
        }
        if (vkToGlSemaphore != null) {
            vkToGlSemaphore.destroy();
            vkToGlSemaphore = null;
        }

        initialized = false;
        INSTANCE = null;
        LOGGER.info("VulkanPresenter shutdown");
    }


    // ══════════════════════════════════════════════════════════════════
    // Present Path
    // ══════════════════════════════════════════════════════════════════

    /**
     * Present the current GL framebuffer via the Vulkan swapchain.
     * Call this INSTEAD of glfwSwapBuffers when {@link #isActive()} is true.
     *
     * <p>Sequence:</p>
     * <ol>
     *   <li>PCL PresentStart marker</li>
     *   <li>GL signals semaphore → VK waits</li>
     *   <li>VK acquires swapchain image</li>
     *   <li>VK present (SL intercepts for DLSS-G)</li>
     *   <li>PCL PresentEnd marker</li>
     * </ol>
     */
    public void present() {
        if (!active || !initialized) return;

        StreamlineManager slm = StreamlineManager.getInstance();

        // PCL: Present start
        if (slm != null && slm.isInitialized()) {
            slm.beforePresent();
        }

        // 1. GL signals semaphore (GL framebuffer is complete)
        glToVkSemaphore.signalOpenGL();

        // 2. Acquire next swapchain image
        int imageIndex = StreamlineNative.NslAcquireNextImage(
                vkToGlSemaphore.getVkSemaphoreHandle()
        );

        if (imageIndex < 0) {
            // Swapchain out of date — recreate
            handleSwapchainRecreation();
            return;
        }

        // 3. Present — Streamline intercepts this for DLSS-G frame generation
        long[] waitSems = new long[]{ glToVkSemaphore.getVkSemaphoreHandle() };
        int res = StreamlineNative.NslPresent(imageIndex, waitSems);

        if (res != 0) {
            // Handle suboptimal/out-of-date
            handleSwapchainRecreation();
        }

        // 4. VK signals semaphore back → GL waits (for next frame safety)
        vkToGlSemaphore.waitOpenGL();

        // PCL: Present end
        if (slm != null && slm.isInitialized()) {
            slm.afterPresent();
        }
    }


    // ══════════════════════════════════════════════════════════════════
    // Resize / Recreation
    // ══════════════════════════════════════════════════════════════════

    /**
     * Handle window resize. Call from WindowMixin.onResize.
     */
    public void onResize(int newWidth, int newHeight) {
        if (!active || !initialized) return;
        if (newWidth == presentWidth && newHeight == presentHeight) return;
        if (newWidth <= 0 || newHeight <= 0) return;

        presentWidth = newWidth;
        presentHeight = newHeight;

        int res = StreamlineNative.NslRecreateSwapchain(newWidth, newHeight);
        if (res != 0) {
            LOGGER.error("Swapchain recreation failed: {}", res);
        } else {
            LOGGER.info("Swapchain recreated: {}x{}", newWidth, newHeight);
        }
    }

    private void handleSwapchainRecreation() {
        int w = MinecraftWindow.getWindowWidth();
        int h = MinecraftWindow.getWindowHeight();
        if (w > 0 && h > 0) {
            onResize(w, h);
        }
    }


    // ══════════════════════════════════════════════════════════════════
    // State
    // ══════════════════════════════════════════════════════════════════

    /** True when the VK present path is active (DLSS-G enabled). */
    public boolean isActive() {
        return active && initialized;
    }

    public boolean isInitialized() {
        return initialized;
    }

    public int getPresentWidth() { return presentWidth; }
    public int getPresentHeight() { return presentHeight; }
}

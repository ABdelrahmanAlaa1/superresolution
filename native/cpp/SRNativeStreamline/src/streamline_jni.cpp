/*
 * Super Resolution — Streamline Integration
 * Copyright (c) 2025-2026. 187J3X1-114514
 *
 * JNI bridge: maps Java StreamlineNative methods to slBridge_* C API.
 */

#define SR_STREAMLINE_EXPORTS
#include "sr/streamline_api.h"

#include <jni.h>
#include <cstring>
#include <cstdlib>

// ── JNI class: io.homo.superresolution.core.StreamlineNative ──
// Method naming: Java_io_homo_superresolution_core_StreamlineNative_<methodName>

// Helper: convert jstring to wchar_t* (Windows)
static wchar_t* jstringToWchar(JNIEnv* env, jstring str) {
    if (!str) return nullptr;
    const jchar* chars = env->GetStringChars(str, nullptr);
    jsize len = env->GetStringLength(str);
    wchar_t* result = new wchar_t[len + 1];
    for (jsize i = 0; i < len; i++) {
        result[i] = (wchar_t)chars[i];
    }
    result[len] = L'\0';
    env->ReleaseStringChars(str, chars);
    return result;
}


// ════════════════════════════════════════════════════════════════════════
// Phase 1: Core
// ════════════════════════════════════════════════════════════════════════

extern "C" JNIEXPORT jint JNICALL
Java_io_homo_superresolution_core_StreamlineNative_NslInit(
    JNIEnv* env, jclass, jstring pluginsPath, jlong appId, jint logLevel
) {
    wchar_t* path = jstringToWchar(env, pluginsPath);
    int result = slBridge_Init(path, (uint64_t)appId, (int)logLevel);
    delete[] path;
    return result;
}

extern "C" JNIEXPORT jint JNICALL
Java_io_homo_superresolution_core_StreamlineNative_NslShutdown(
    JNIEnv*, jclass
) {
    return slBridge_Shutdown();
}

extern "C" JNIEXPORT jint JNICALL
Java_io_homo_superresolution_core_StreamlineNative_NslSetVulkanInfo(
    JNIEnv*, jclass,
    jlong instance, jlong physDev, jlong device,
    jint gfxQueueFamily, jint gfxQueueIdx
) {
    return slBridge_SetVulkanInfo(
        (uint64_t)instance, (uint64_t)physDev, (uint64_t)device,
        (uint32_t)gfxQueueFamily, (uint32_t)gfxQueueIdx
    );
}

extern "C" JNIEXPORT jboolean JNICALL
Java_io_homo_superresolution_core_StreamlineNative_NslIsFeatureSupported(
    JNIEnv*, jclass, jint featureId
) {
    return slBridge_IsFeatureSupported((uint32_t)featureId) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jlong JNICALL
Java_io_homo_superresolution_core_StreamlineNative_NslGetNewFrameToken(
    JNIEnv*, jclass, jint frameIndex
) {
    uint64_t handle = 0;
    int res = slBridge_GetNewFrameToken((uint32_t)frameIndex, &handle);
    return (res == 0) ? (jlong)handle : 0L;
}


// ════════════════════════════════════════════════════════════════════════
// Phase 2: Reflex
// ════════════════════════════════════════════════════════════════════════

extern "C" JNIEXPORT jint JNICALL
Java_io_homo_superresolution_core_StreamlineNative_NslReflexSetOptions(
    JNIEnv*, jclass, jint mode, jint frameLimitUs
) {
    return slBridge_ReflexSetOptions((int)mode, (uint32_t)frameLimitUs);
}

extern "C" JNIEXPORT jint JNICALL
Java_io_homo_superresolution_core_StreamlineNative_NslReflexSleep(
    JNIEnv*, jclass, jlong frameToken
) {
    return slBridge_ReflexSleep((uint64_t)frameToken);
}

extern "C" JNIEXPORT jfloatArray JNICALL
Java_io_homo_superresolution_core_StreamlineNative_NslReflexGetState(
    JNIEnv* env, jclass
) {
    int lowLatAvail = 0;
    float gpuActiveMs = 0, gpuFrameMs = 0, pcLatencyMs = 0;
    int res = slBridge_ReflexGetState(&lowLatAvail, &gpuActiveMs, &gpuFrameMs, &pcLatencyMs);

    jfloatArray result = env->NewFloatArray(4);
    if (res == 0) {
        float data[4] = {
            (float)lowLatAvail,
            gpuActiveMs,
            gpuFrameMs,
            pcLatencyMs
        };
        env->SetFloatArrayRegion(result, 0, 4, data);
    }
    return result;
}

extern "C" JNIEXPORT jint JNICALL
Java_io_homo_superresolution_core_StreamlineNative_NslReflexSetCameraData(
    JNIEnv* env, jclass, jlong frameToken, jfloatArray viewToClip, jfloatArray worldToView
) {
    jfloat* vtc = env->GetFloatArrayElements(viewToClip, nullptr);
    jfloat* wtv = env->GetFloatArrayElements(worldToView, nullptr);
    int res = slBridge_ReflexSetCameraData((uint64_t)frameToken, vtc, wtv);
    env->ReleaseFloatArrayElements(viewToClip, vtc, JNI_ABORT);
    env->ReleaseFloatArrayElements(worldToView, wtv, JNI_ABORT);
    return res;
}


// ════════════════════════════════════════════════════════════════════════
// Phase 2: PCL
// ════════════════════════════════════════════════════════════════════════

extern "C" JNIEXPORT jint JNICALL
Java_io_homo_superresolution_core_StreamlineNative_NslPCLSetMarker(
    JNIEnv*, jclass, jint markerType, jlong frameToken
) {
    return slBridge_PCLSetMarker((uint32_t)markerType, (uint64_t)frameToken);
}

extern "C" JNIEXPORT jint JNICALL
Java_io_homo_superresolution_core_StreamlineNative_NslPCLGetStatsWindowMessage(
    JNIEnv*, jclass
) {
    uint32_t msg = 0;
    slBridge_PCLGetState(&msg);
    return (jint)msg;
}


// ════════════════════════════════════════════════════════════════════════
// Phase 3: Swapchain
// ════════════════════════════════════════════════════════════════════════

extern "C" JNIEXPORT jint JNICALL
Java_io_homo_superresolution_core_StreamlineNative_NslCreateSwapchain(
    JNIEnv*, jclass,
    jlong instance, jlong physDev, jlong device,
    jlong surface, jint w, jint h, jint format, jint presentQueueFamily
) {
    return slBridge_CreateSwapchain(
        (uint64_t)instance, (uint64_t)physDev, (uint64_t)device,
        (uint64_t)surface, (uint32_t)w, (uint32_t)h,
        (uint32_t)format, (uint32_t)presentQueueFamily
    );
}

extern "C" JNIEXPORT jint JNICALL
Java_io_homo_superresolution_core_StreamlineNative_NslDestroySwapchain(
    JNIEnv*, jclass
) {
    return slBridge_DestroySwapchain();
}

extern "C" JNIEXPORT jint JNICALL
Java_io_homo_superresolution_core_StreamlineNative_NslAcquireNextImage(
    JNIEnv*, jclass, jlong semaphore
) {
    uint32_t imageIndex = 0;
    int res = slBridge_AcquireNextImage((uint64_t)semaphore, &imageIndex);
    return (res == 0) ? (jint)imageIndex : -1;
}

extern "C" JNIEXPORT jint JNICALL
Java_io_homo_superresolution_core_StreamlineNative_NslPresent(
    JNIEnv* env, jclass, jint imageIndex, jlongArray waitSemaphores
) {
    jsize count = waitSemaphores ? env->GetArrayLength(waitSemaphores) : 0;
    jlong* sems = count > 0 ? env->GetLongArrayElements(waitSemaphores, nullptr) : nullptr;
    int res = slBridge_Present((uint32_t)imageIndex, (uint64_t*)sems, (uint32_t)count);
    if (sems) env->ReleaseLongArrayElements(waitSemaphores, sems, JNI_ABORT);
    return res;
}

extern "C" JNIEXPORT jint JNICALL
Java_io_homo_superresolution_core_StreamlineNative_NslRecreateSwapchain(
    JNIEnv*, jclass, jint w, jint h
) {
    return slBridge_RecreateSwapchain((uint32_t)w, (uint32_t)h);
}

extern "C" JNIEXPORT jlongArray JNICALL
Java_io_homo_superresolution_core_StreamlineNative_NslGetSwapchainImages(
    JNIEnv* env, jclass
) {
    uint64_t images[8];
    uint32_t count = 8;
    int res = slBridge_GetSwapchainImages(images, &count);
    if (res != 0 || count == 0) {
        return env->NewLongArray(0);
    }
    jlongArray result = env->NewLongArray(count);
    env->SetLongArrayRegion(result, 0, count, (jlong*)images);
    return result;
}


// ════════════════════════════════════════════════════════════════════════
// Phase 4: Constants + Tagging + DLSS-G
// ════════════════════════════════════════════════════════════════════════

extern "C" JNIEXPORT jint JNICALL
Java_io_homo_superresolution_core_StreamlineNative_NslSetConstants(
    JNIEnv* env, jclass,
    jlong frameToken, jint viewportId,
    jfloatArray clipToCamView, jfloatArray camViewToClip,
    jfloatArray worldToCamView, jfloatArray camViewToWorld,
    jfloatArray prevClipToCamView, jfloatArray prevCamViewToClip,
    jfloat jitterX, jfloat jitterY,
    jfloat mvecScaleX, jfloat mvecScaleY,
    jfloat cameraNear, jfloat cameraFar, jfloat cameraFOV, jfloat aspectRatio,
    jint renderW, jint renderH, jboolean reset
) {
    jfloat* m1 = env->GetFloatArrayElements(clipToCamView, nullptr);
    jfloat* m2 = env->GetFloatArrayElements(camViewToClip, nullptr);
    jfloat* m3 = env->GetFloatArrayElements(worldToCamView, nullptr);
    jfloat* m4 = env->GetFloatArrayElements(camViewToWorld, nullptr);
    jfloat* m5 = env->GetFloatArrayElements(prevClipToCamView, nullptr);
    jfloat* m6 = env->GetFloatArrayElements(prevCamViewToClip, nullptr);

    int res = slBridge_SetConstants(
        (uint64_t)frameToken, (uint32_t)viewportId,
        m1, m2, m3, m4, m5, m6,
        jitterX, jitterY, mvecScaleX, mvecScaleY,
        cameraNear, cameraFar, cameraFOV, aspectRatio,
        (uint32_t)renderW, (uint32_t)renderH, reset ? 1 : 0
    );

    env->ReleaseFloatArrayElements(clipToCamView, m1, JNI_ABORT);
    env->ReleaseFloatArrayElements(camViewToClip, m2, JNI_ABORT);
    env->ReleaseFloatArrayElements(worldToCamView, m3, JNI_ABORT);
    env->ReleaseFloatArrayElements(camViewToWorld, m4, JNI_ABORT);
    env->ReleaseFloatArrayElements(prevClipToCamView, m5, JNI_ABORT);
    env->ReleaseFloatArrayElements(prevCamViewToClip, m6, JNI_ABORT);
    return res;
}

extern "C" JNIEXPORT jint JNICALL
Java_io_homo_superresolution_core_StreamlineNative_NslTagResources(
    JNIEnv*, jclass,
    jlong frameToken, jint viewportId, jlong cmdBuf,
    jlong depthImg, jlong depthView, jint dW, jint dH, jint dFmt,
    jlong mvecImg, jlong mvecView, jint mW, jint mH, jint mFmt,
    jlong hudlessImg, jlong hudlessView, jint hW, jint hH, jint hFmt,
    jlong uiImg, jlong uiView, jint uW, jint uH, jint uFmt,
    jlong exposureImg, jlong exposureView, jint eW, jint eH, jint eFmt,
    jlong colorInImg, jlong colorInView, jint ciW, jint ciH, jint ciFmt,
    jlong colorOutImg, jlong colorOutView, jint coW, jint coH, jint coFmt
) {
    return slBridge_TagResources(
        (uint64_t)frameToken, (uint32_t)viewportId, (uint64_t)cmdBuf,
        (uint64_t)depthImg, (uint64_t)depthView, (uint32_t)dW, (uint32_t)dH, (uint32_t)dFmt,
        (uint64_t)mvecImg, (uint64_t)mvecView, (uint32_t)mW, (uint32_t)mH, (uint32_t)mFmt,
        (uint64_t)hudlessImg, (uint64_t)hudlessView, (uint32_t)hW, (uint32_t)hH, (uint32_t)hFmt,
        (uint64_t)uiImg, (uint64_t)uiView, (uint32_t)uW, (uint32_t)uH, (uint32_t)uFmt,
        (uint64_t)exposureImg, (uint64_t)exposureView, (uint32_t)eW, (uint32_t)eH, (uint32_t)eFmt,
        (uint64_t)colorInImg, (uint64_t)colorInView, (uint32_t)ciW, (uint32_t)ciH, (uint32_t)ciFmt,
        (uint64_t)colorOutImg, (uint64_t)colorOutView, (uint32_t)coW, (uint32_t)coH, (uint32_t)coFmt
    );
}

extern "C" JNIEXPORT jint JNICALL
Java_io_homo_superresolution_core_StreamlineNative_NslDLSSGSetOptions(
    JNIEnv*, jclass, jint viewportId, jint mode, jint numFrames
) {
    return slBridge_DLSSGSetOptions((uint32_t)viewportId, (int)mode, (uint32_t)numFrames);
}

extern "C" JNIEXPORT jlongArray JNICALL
Java_io_homo_superresolution_core_StreamlineNative_NslDLSSGGetState(
    JNIEnv* env, jclass, jint viewportId
) {
    uint64_t vram = 0;
    uint32_t status = 0, minDim = 0, maxFrames = 0;
    int res = slBridge_DLSSGGetState((uint32_t)viewportId, &vram, &status, &minDim, &maxFrames);

    jlongArray result = env->NewLongArray(4);
    if (res == 0) {
        jlong data[4] = { (jlong)vram, (jlong)status, (jlong)minDim, (jlong)maxFrames };
        env->SetLongArrayRegion(result, 0, 4, data);
    }
    return result;
}


// ════════════════════════════════════════════════════════════════════════
// Future: Evaluate + Resource Lifecycle
// ════════════════════════════════════════════════════════════════════════

extern "C" JNIEXPORT jint JNICALL
Java_io_homo_superresolution_core_StreamlineNative_NslEvaluateFeature(
    JNIEnv*, jclass, jint featureId, jlong frameToken, jint viewportId, jlong cmdBuf
) {
    return slBridge_EvaluateFeature(
        (uint32_t)featureId, (uint64_t)frameToken,
        (uint32_t)viewportId, (uint64_t)cmdBuf
    );
}

extern "C" JNIEXPORT jint JNICALL
Java_io_homo_superresolution_core_StreamlineNative_NslAllocateResources(
    JNIEnv*, jclass, jint featureId, jint viewportId, jlong cmdBuf
) {
    return slBridge_AllocateResources((uint32_t)featureId, (uint32_t)viewportId, (uint64_t)cmdBuf);
}

extern "C" JNIEXPORT jint JNICALL
Java_io_homo_superresolution_core_StreamlineNative_NslFreeResources(
    JNIEnv*, jclass, jint featureId, jint viewportId
) {
    return slBridge_FreeResources((uint32_t)featureId, (uint32_t)viewportId);
}

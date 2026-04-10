/*
 * Super Resolution — Streamline Integration
 * Copyright (c) 2025-2026. 187J3X1-114514
 *
 * DLSS-G (Frame Generation), common constants, and resource tagging.
 */

#define SR_STREAMLINE_EXPORTS
#include "sr/streamline_api.h"

#include <sl.h>
#include <sl_consts.h>
#include <sl_core_api.h>
#include <sl_core_types.h>
#include <sl_dlss_g.h>

#include <cstring>
#include <cstdio>
#include <vector>

// ── External state from streamline_core.cpp ──
extern bool g_slInitialized;

static inline sl::FrameToken* tokenFromHandle(uint64_t handle) {
    return reinterpret_cast<sl::FrameToken*>(handle);
}

// ── Helper: create sl::Resource from VK handles ──
static sl::Resource makeVkResource(uint64_t image, uint64_t view,
                                    uint32_t w, uint32_t h, uint32_t fmt) {
    sl::Resource res{};
    res.type = sl::ResourceType::eTex2d;
    res.native = (void*)image;
    res.view = (void*)view;
    res.width = w;
    res.height = h;
    res.format = fmt;
    res.state = 0; // VK_IMAGE_LAYOUT_UNDEFINED — SL manages transitions
    return res;
}


// ════════════════════════════════════════════════════════════════════════
// Common Constants
// ════════════════════════════════════════════════════════════════════════

extern "C" SL_BRIDGE_API int slBridge_SetConstants(
    uint64_t frameTokenHandle,
    uint32_t viewportId,
    const float* clipToCameraView,
    const float* cameraViewToClip,
    const float* worldToCameraView,
    const float* cameraViewToWorld,
    const float* prevClipToCameraView,
    const float* prevCameraViewToClip,
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
) {
    if (!g_slInitialized) return -1;

    sl::FrameToken* token = tokenFromHandle(frameTokenHandle);
    if (!token) return -1;

    sl::Constants consts{};

    // Camera matrices — all row-major (Java side transposes before calling)
    if (clipToCameraView)    memcpy(&consts.clipToCameraView, clipToCameraView, 64);
    if (cameraViewToClip)    memcpy(&consts.cameraViewToClip, cameraViewToClip, 64);
    if (worldToCameraView)   memcpy(&consts.worldToCameraView, worldToCameraView, 64);
    if (cameraViewToWorld)   memcpy(&consts.cameraViewToWorld, cameraViewToWorld, 64);
    if (prevClipToCameraView) memcpy(&consts.prevClipToCameraView, prevClipToCameraView, 64);
    if (prevCameraViewToClip) memcpy(&consts.prevCameraViewToClip, prevCameraViewToClip, 64);

    // Jitter offset (pixel space)
    consts.jitterOffset = {jitterOffsetX, jitterOffsetY};

    // Motion vector scaling:
    //   pixel space mvecs: {1.0f/renderW, 1.0f/renderH}
    //   normalized mvecs:  {1.0f, 1.0f}
    consts.mvecScale = {mvecScaleX, mvecScaleY};

    consts.cameraPinholeOffset = {0.0f, 0.0f};
    consts.cameraNear = cameraNear;
    consts.cameraFar = cameraFar;
    consts.cameraFOV = cameraFOV;
    consts.cameraAspectRatio = aspectRatio;

    // Minecraft uses reverse-Z depth
    consts.depthInverted = sl::Boolean::eTrue;

    // Motion vectors include camera motion
    consts.cameraMotionIncluded = sl::Boolean::eTrue;

    // 2D motion vectors
    consts.motionVectors3D = sl::Boolean::eFalse;

    // Reset on scene cut / teleport / world join
    consts.reset = reset ? sl::Boolean::eTrue : sl::Boolean::eFalse;

    sl::ViewportHandle viewport(viewportId);
    sl::Result res = slSetConstants(consts, *token, viewport);
    if (res != sl::Result::eOk) {
        fprintf(stderr, "[SL ERROR] slSetConstants failed: %d\n", (int)res);
        return (int)res;
    }
    return 0;
}


// ════════════════════════════════════════════════════════════════════════
// Resource Tagging
// ════════════════════════════════════════════════════════════════════════

extern "C" SL_BRIDGE_API int slBridge_TagResources(
    uint64_t frameTokenHandle,
    uint32_t viewportId,
    uint64_t cmdBuffer,
    // Depth
    uint64_t depthImage, uint64_t depthView,
    uint32_t depthW, uint32_t depthH, uint32_t depthFmt,
    // Motion vectors
    uint64_t mvecImage, uint64_t mvecView,
    uint32_t mvecW, uint32_t mvecH, uint32_t mvecFmt,
    // HUDless color
    uint64_t hudlessImage, uint64_t hudlessView,
    uint32_t hudlessW, uint32_t hudlessH, uint32_t hudlessFmt,
    // UI color + alpha
    uint64_t uiImage, uint64_t uiView,
    uint32_t uiW, uint32_t uiH, uint32_t uiFmt,
    // Exposure
    uint64_t exposureImage, uint64_t exposureView,
    uint32_t exposureW, uint32_t exposureH, uint32_t exposureFmt,
    // Scaling input color (future)
    uint64_t colorInImage, uint64_t colorInView,
    uint32_t colorInW, uint32_t colorInH, uint32_t colorInFmt,
    // Scaling output color (future)
    uint64_t colorOutImage, uint64_t colorOutView,
    uint32_t colorOutW, uint32_t colorOutH, uint32_t colorOutFmt
) {
    if (!g_slInitialized) return -1;

    sl::FrameToken* token = tokenFromHandle(frameTokenHandle);
    if (!token) return -1;

    std::vector<sl::ResourceTag> tags;
    std::vector<sl::Resource> resources; // keep alive until slSetTagForFrame
    std::vector<sl::Extent> extents;

    // Helper lambda to add a resource tag
    auto addTag = [&](uint64_t img, uint64_t view, uint32_t w, uint32_t h,
                       uint32_t fmt, sl::BufferType bufType,
                       sl::ResourceLifecycle lifecycle) {
        if (img == 0) return;
        resources.push_back(makeVkResource(img, view, w, h, fmt));
        extents.push_back({0, 0, w, h});
        sl::ResourceTag tag{};
        tag.resource = &resources.back();
        tag.type = bufType;
        tag.lifecycle = lifecycle;
        tag.extent = &extents.back();
        tags.push_back(tag);
    };

    // -- DLSS-G required buffers --
    addTag(depthImage, depthView, depthW, depthH, depthFmt,
           sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent);

    addTag(mvecImage, mvecView, mvecW, mvecH, mvecFmt,
           sl::kBufferTypeMvec, sl::ResourceLifecycle::eOnlyValidNow);

    addTag(hudlessImage, hudlessView, hudlessW, hudlessH, hudlessFmt,
           sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent);

    addTag(uiImage, uiView, uiW, uiH, uiFmt,
           sl::kBufferTypeUIColorAndAlpha, sl::ResourceLifecycle::eValidUntilPresent);

    // -- Exposure (DLSS SR, optional DLSS-G) --
    addTag(exposureImage, exposureView, exposureW, exposureH, exposureFmt,
           sl::kBufferTypeExposure, sl::ResourceLifecycle::eOnlyValidNow);

    // -- Scaling I/O (future: DLSS SR, RR, NIS, DeepDVC) --
    addTag(colorInImage, colorInView, colorInW, colorInH, colorInFmt,
           sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eOnlyValidNow);

    addTag(colorOutImage, colorOutView, colorOutW, colorOutH, colorOutFmt,
           sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eOnlyValidNow);

    if (tags.empty()) return 0;

    sl::ViewportHandle viewport(viewportId);
    sl::CommandBuffer* cmd = (sl::CommandBuffer*)cmdBuffer;

    sl::Result res = slSetTagForFrame(*token, viewport, tags.data(),
                                      (uint32_t)tags.size(), cmd);
    if (res != sl::Result::eOk) {
        fprintf(stderr, "[SL ERROR] slSetTagForFrame failed: %d\n", (int)res);
        return (int)res;
    }
    return 0;
}


// ════════════════════════════════════════════════════════════════════════
// DLSS-G (Frame Generation)
// ════════════════════════════════════════════════════════════════════════

extern "C" SL_BRIDGE_API int slBridge_DLSSGSetOptions(
    uint32_t viewportId,
    int mode,
    uint32_t numFramesToGenerate
) {
    if (!g_slInitialized) return -1;

    sl::DLSSGOptions options{};
    switch (mode) {
        case 0: options.mode = sl::DLSSGMode::eOff; break;
        case 1: options.mode = sl::DLSSGMode::eOn; break;
        case 2: options.mode = sl::DLSSGMode::eAuto; break;
        default: options.mode = sl::DLSSGMode::eOff; break;
    }
    options.numFramesToGenerate = numFramesToGenerate;

    sl::ViewportHandle viewport(viewportId);
    sl::Result res = slDLSSGSetOptions(viewport, options);
    if (res != sl::Result::eOk) {
        fprintf(stderr, "[SL ERROR] slDLSSGSetOptions failed: %d\n", (int)res);
        return (int)res;
    }
    return 0;
}

extern "C" SL_BRIDGE_API int slBridge_DLSSGGetState(
    uint32_t viewportId,
    uint64_t* outEstimatedVRAM,
    uint32_t* outStatus,
    uint32_t* outMinWidthOrHeight,
    uint32_t* outNumFramesToGenerateMax
) {
    if (!g_slInitialized) return -1;

    sl::DLSSGState state{};
    sl::ViewportHandle viewport(viewportId);
    sl::Result res = slDLSSGGetState(viewport, state);
    if (res != sl::Result::eOk) {
        return (int)res;
    }

    if (outEstimatedVRAM)          *outEstimatedVRAM = state.estimatedVRAMUsageInBytes;
    if (outStatus)                 *outStatus = (uint32_t)state.status;
    if (outMinWidthOrHeight)       *outMinWidthOrHeight = state.minWidthOrHeight;
    if (outNumFramesToGenerateMax) *outNumFramesToGenerateMax = state.numFramesToGenerateMax;

    return 0;
}


// ════════════════════════════════════════════════════════════════════════
// Future: Feature Evaluation + Resource Lifecycle
// ════════════════════════════════════════════════════════════════════════

extern "C" SL_BRIDGE_API int slBridge_EvaluateFeature(
    uint32_t featureId,
    uint64_t frameTokenHandle,
    uint32_t viewportId,
    uint64_t cmdBuffer
) {
    if (!g_slInitialized) return -1;

    sl::FrameToken* token = tokenFromHandle(frameTokenHandle);
    if (!token) return -1;

    sl::ViewportHandle viewport(viewportId);
    const sl::BaseStructure* inputs[] = { &viewport };
    sl::CommandBuffer* cmd = (sl::CommandBuffer*)cmdBuffer;

    sl::Result res = slEvaluateFeature((sl::Feature)featureId, *token,
                                       inputs, 1, cmd);
    if (res != sl::Result::eOk) {
        fprintf(stderr, "[SL ERROR] slEvaluateFeature(%u) failed: %d\n",
                featureId, (int)res);
        return (int)res;
    }
    return 0;
}

extern "C" SL_BRIDGE_API int slBridge_AllocateResources(
    uint32_t featureId,
    uint32_t viewportId,
    uint64_t cmdBuffer
) {
    if (!g_slInitialized) return -1;

    sl::ViewportHandle viewport(viewportId);
    sl::CommandBuffer* cmd = (sl::CommandBuffer*)cmdBuffer;
    sl::Result res = slAllocateResources(cmd, (sl::Feature)featureId, viewport);
    if (res != sl::Result::eOk) {
        fprintf(stderr, "[SL ERROR] slAllocateResources(%u) failed: %d\n",
                featureId, (int)res);
        return (int)res;
    }
    return 0;
}

extern "C" SL_BRIDGE_API int slBridge_FreeResources(
    uint32_t featureId,
    uint32_t viewportId
) {
    if (!g_slInitialized) return -1;

    sl::ViewportHandle viewport(viewportId);
    sl::Result res = slFreeResources((sl::Feature)featureId, viewport);
    if (res != sl::Result::eOk) {
        fprintf(stderr, "[SL ERROR] slFreeResources(%u) failed: %d\n",
                featureId, (int)res);
        return (int)res;
    }
    return 0;
}


// ════════════════════════════════════════════════════════════════════════
// Future: DLSS-RR Additional Resource Tagging
// ════════════════════════════════════════════════════════════════════════

extern "C" SL_BRIDGE_API int slBridge_TagDLSSRRResources(
    uint64_t frameTokenHandle,
    uint32_t viewportId,
    uint64_t cmdBuffer,
    uint64_t albedoImage, uint64_t albedoView,
    uint32_t albedoW, uint32_t albedoH, uint32_t albedoFmt,
    uint64_t specAlbedoImage, uint64_t specAlbedoView,
    uint32_t specAlbedoW, uint32_t specAlbedoH, uint32_t specAlbedoFmt,
    uint64_t normalRoughImage, uint64_t normalRoughView,
    uint32_t normalRoughW, uint32_t normalRoughH, uint32_t normalRoughFmt,
    uint64_t specMvecImage, uint64_t specMvecView,
    uint32_t specMvecW, uint32_t specMvecH, uint32_t specMvecFmt
) {
    if (!g_slInitialized) return -1;

    sl::FrameToken* token = tokenFromHandle(frameTokenHandle);
    if (!token) return -1;

    std::vector<sl::ResourceTag> tags;
    std::vector<sl::Resource> resources;
    std::vector<sl::Extent> extents;

    auto addTag = [&](uint64_t img, uint64_t view, uint32_t w, uint32_t h,
                       uint32_t fmt, sl::BufferType bufType) {
        if (img == 0) return;
        resources.push_back(makeVkResource(img, view, w, h, fmt));
        extents.push_back({0, 0, w, h});
        sl::ResourceTag tag{};
        tag.resource = &resources.back();
        tag.type = bufType;
        tag.lifecycle = sl::ResourceLifecycle::eOnlyValidNow;
        tag.extent = &extents.back();
        tags.push_back(tag);
    };

    addTag(albedoImage, albedoView, albedoW, albedoH, albedoFmt,
           sl::kBufferTypeAlbedo);
    addTag(specAlbedoImage, specAlbedoView, specAlbedoW, specAlbedoH, specAlbedoFmt,
           sl::kBufferTypeSpecularAlbedo);
    addTag(normalRoughImage, normalRoughView, normalRoughW, normalRoughH, normalRoughFmt,
           sl::kBufferTypeNormalRoughness);
    addTag(specMvecImage, specMvecView, specMvecW, specMvecH, specMvecFmt,
           sl::kBufferTypeSpecularMotionVectors);

    if (tags.empty()) return 0;

    sl::ViewportHandle viewport(viewportId);
    sl::CommandBuffer* cmd = (sl::CommandBuffer*)cmdBuffer;

    sl::Result res = slSetTagForFrame(*token, viewport, tags.data(),
                                      (uint32_t)tags.size(), cmd);
    if (res != sl::Result::eOk) {
        fprintf(stderr, "[SL ERROR] slSetTagForFrame (DLSS-RR) failed: %d\n", (int)res);
        return (int)res;
    }
    return 0;
}

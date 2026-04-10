/*
 * Super Resolution — Streamline Integration
 * Copyright (c) 2025-2026. 187J3X1-114514
 *
 * Reflex (low latency) and PCL (PC Latency markers) implementation.
 */

#define SR_STREAMLINE_EXPORTS
#include "sr/streamline_api.h"

#include <sl.h>
#include <sl_consts.h>
#include <sl_core_api.h>
#include <sl_reflex.h>
#include <sl_pcl.h>

#include <cstdio>

// ── External state from streamline_core.cpp ──
extern bool g_slInitialized;

// ── Helper: recover FrameToken pointer from opaque handle ──
static inline sl::FrameToken* tokenFromHandle(uint64_t handle) {
    return reinterpret_cast<sl::FrameToken*>(handle);
}

// ── Map our bridge PCL marker enum to sl::PCLMarker ──
static sl::PCLMarker mapPCLMarker(uint32_t marker) {
    switch (marker) {
        case SL_BRIDGE_PCL_SIMULATION_START:    return sl::PCLMarker::eSimulationStart;
        case SL_BRIDGE_PCL_SIMULATION_END:      return sl::PCLMarker::eSimulationEnd;
        case SL_BRIDGE_PCL_RENDER_SUBMIT_START: return sl::PCLMarker::eRenderSubmitStart;
        case SL_BRIDGE_PCL_RENDER_SUBMIT_END:   return sl::PCLMarker::eRenderSubmitEnd;
        case SL_BRIDGE_PCL_PRESENT_START:       return sl::PCLMarker::ePresentStart;
        case SL_BRIDGE_PCL_PRESENT_END:         return sl::PCLMarker::ePresentEnd;
        case SL_BRIDGE_PCL_INPUT_SAMPLE:        return sl::PCLMarker::eInputSample;
        case SL_BRIDGE_PCL_TRIGGER_FLASH:       return sl::PCLMarker::eTriggerFlash;
        case SL_BRIDGE_PCL_PC_LATENCY_PING:     return sl::PCLMarker::ePCLatencyPing;
        default:                                return sl::PCLMarker::eSimulationStart;
    }
}


// ════════════════════════════════════════════════════════════════════════
// Reflex
// ════════════════════════════════════════════════════════════════════════

extern "C" SL_BRIDGE_API int slBridge_ReflexSetOptions(int mode, uint32_t frameLimitUs) {
    if (!g_slInitialized) return -1;

    sl::ReflexOptions options{};
    switch (mode) {
        case 0: options.mode = sl::ReflexMode::eOff; break;
        case 1: options.mode = sl::ReflexMode::eLowLatency; break;
        case 2: options.mode = sl::ReflexMode::eLowLatencyWithBoost; break;
        default: options.mode = sl::ReflexMode::eOff; break;
    }
    options.frameLimitUs = frameLimitUs;

    sl::Result res = slReflexSetOptions(options);
    if (res != sl::Result::eOk) {
        fprintf(stderr, "[SL ERROR] slReflexSetOptions failed: %d\n", (int)res);
        return (int)res;
    }
    return 0;
}

extern "C" SL_BRIDGE_API int slBridge_ReflexSleep(uint64_t frameTokenHandle) {
    if (!g_slInitialized) return -1;

    sl::FrameToken* token = tokenFromHandle(frameTokenHandle);
    if (!token) return -1;

    sl::Result res = slReflexSleep(*token);
    if (res != sl::Result::eOk) {
        // Don't spam logs for sleep — may fail when Reflex is off (expected)
        return (int)res;
    }
    return 0;
}

extern "C" SL_BRIDGE_API int slBridge_ReflexGetState(
    int* outLowLatAvail,
    float* outGpuActiveMs,
    float* outGpuFrameMs,
    float* outPcLatencyMs
) {
    if (!g_slInitialized) return -1;

    sl::ReflexState state{};
    sl::Result res = slReflexGetState(state);
    if (res != sl::Result::eOk) {
        return (int)res;
    }

    if (outLowLatAvail)  *outLowLatAvail = state.lowLatencyAvailable ? 1 : 0;
    if (outGpuActiveMs)  *outGpuActiveMs = state.frameReport[0].gpuActiveRenderTimeUs / 1000.0f;
    if (outGpuFrameMs)   *outGpuFrameMs  = state.frameReport[0].gpuFrameTimeUs / 1000.0f;
    if (outPcLatencyMs)  *outPcLatencyMs  = state.frameReport[0].pcLatencyMs;

    return 0;
}

extern "C" SL_BRIDGE_API int slBridge_ReflexSetCameraData(
    uint64_t frameTokenHandle,
    const float* viewToClip4x4,
    const float* worldToView4x4
) {
    if (!g_slInitialized) return -1;

    sl::FrameToken* token = tokenFromHandle(frameTokenHandle);
    if (!token) return -1;

    sl::ReflexCameraData cameraData{};
    if (viewToClip4x4) {
        memcpy(&cameraData.cameraViewToClip, viewToClip4x4, sizeof(float) * 16);
    }
    if (worldToView4x4) {
        memcpy(&cameraData.worldToCameraView, worldToView4x4, sizeof(float) * 16);
    }

    sl::ViewportHandle viewport(0);
    sl::Result res = slReflexSetCameraData(viewport, *token, cameraData);
    if (res != sl::Result::eOk) {
        fprintf(stderr, "[SL ERROR] slReflexSetCameraData failed: %d\n", (int)res);
        return (int)res;
    }
    return 0;
}

extern "C" SL_BRIDGE_API int slBridge_ReflexGetPredictedCameraData(
    uint64_t frameTokenHandle,
    float* outPredictedViewToClip4x4,
    float* outPredictedWorldToView4x4
) {
    if (!g_slInitialized) return -1;

    sl::FrameToken* token = tokenFromHandle(frameTokenHandle);
    if (!token) return -1;

    sl::ReflexPredictedCameraData predicted{};
    sl::ViewportHandle viewport(0);
    sl::Result res = slReflexGetPredictedCameraData(viewport, *token, predicted);
    if (res != sl::Result::eOk) {
        return (int)res;
    }

    if (outPredictedViewToClip4x4) {
        memcpy(outPredictedViewToClip4x4, &predicted.cameraViewToClip, sizeof(float) * 16);
    }
    if (outPredictedWorldToView4x4) {
        memcpy(outPredictedWorldToView4x4, &predicted.worldToCameraView, sizeof(float) * 16);
    }
    return 0;
}


// ════════════════════════════════════════════════════════════════════════
// PCL (PC Latency) Markers
// ════════════════════════════════════════════════════════════════════════

extern "C" SL_BRIDGE_API int slBridge_PCLSetMarker(uint32_t marker, uint64_t frameTokenHandle) {
    if (!g_slInitialized) return -1;

    sl::FrameToken* token = tokenFromHandle(frameTokenHandle);
    if (!token) return -1;

    sl::PCLMarker pclMarker = mapPCLMarker(marker);
    sl::Result res = slPCLSetMarker(pclMarker, *token);
    if (res != sl::Result::eOk) {
        // PCL failures are non-fatal, log only at verbose
        return (int)res;
    }
    return 0;
}

extern "C" SL_BRIDGE_API int slBridge_PCLGetState(uint32_t* outStatsWindowMessage) {
    if (!g_slInitialized || !outStatsWindowMessage) return -1;

    sl::PCLState state{};
    sl::Result res = slPCLGetState(state);
    if (res != sl::Result::eOk) {
        *outStatsWindowMessage = 0;
        return (int)res;
    }

    *outStatsWindowMessage = state.statsWindowMessage;
    return 0;
}

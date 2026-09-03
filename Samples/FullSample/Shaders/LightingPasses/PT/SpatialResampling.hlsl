/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#pragma pack_matrix(row_major)

#include "../RtxdiApplicationBridge/RtxdiApplicationBridge.hlsli"
#define RTXDI_RESTIR_PT_HYBRID_SHIFT
#include "../RtxdiApplicationBridge/PathTracer/RAB_PathTracer.hlsli"
#include "../../ShaderDebug/ShaderDebugPrint/ShaderDebugPrint.hlsli"

#include <Rtxdi/PT/SpatialResampling.hlsli>
#include <Rtxdi/PT/DuplicationMap.hlsli>

#if USE_RAY_QUERY
#define RTXDI_ENABLE_BOILING_FILTER
#define RTXDI_BOILING_FILTER_GROUP_SIZE RTXDI_SCREEN_SPACE_GROUP_SIZE
#endif
#include <Rtxdi/PT/Decorrelation.hlsli>

#if USE_RAY_QUERY
[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 globalIndex : SV_DispatchThreadID, uint2 localIndex : SV_GroupThreadID)
#else
[shader("raygeneration")]
void RayGen()
#endif
{
#if !USE_RAY_QUERY
    const uint2 globalIndex = DispatchRaysIndex().xy;
    const uint2 localIndex = uint2(0, 0);
#endif
    const uint2 pixelPosition = RTXDI_ReservoirPosToPixelPos(globalIndex, g_Const.runtimeParams.activeCheckerboardField);
    const uint2 reservoirPosition = RTXDI_PixelPosToReservoirPos(pixelPosition, g_Const.runtimeParams.activeCheckerboardField);

    ShaderDebug::SetDebugShaderPrintCurrentThreadCursorXY(pixelPosition);
    if (all(pixelPosition == g_Const.debug.mouseSelectedPixel))
    {
        Debug_EnablePTPathRecording();
    }

    RTXDI_PTSpatialResamplingRuntimeParameters srrParams = RTXDI_EmptyPTSpatialResamplingRuntimeParameters();
    srrParams.pixelPosition = pixelPosition;
    srrParams.reservoirPosition = reservoirPosition;
    srrParams.cameraPos = g_Const.view.cameraDirectionOrPosition.xyz;
    srrParams.prevCameraPos = g_Const.prevView.cameraDirectionOrPosition.xyz;
    srrParams.prevPrevCameraPos = g_Const.prevPrevView.cameraDirectionOrPosition.xyz;
    srrParams.viewportSize = g_Const.view.viewportSize;

    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(pixelPosition, g_Const.runtimeParams.frameIndex, RTXDI_PT_SPATIAL_RESAMPLING_RANDOM_SEED);
    RAB_PathTracerUserData ptud = RAB_EmptyPathTracerUserData();
    ptud.psr.primarySurfaceWorldPos = float3(0, 0, 0);
    ptud.psr.primarySurfaceViewDir = float3(0, 0, 0);
    ptud.psr.updateWithResampling = g_Const.updatePSRwithResampling;
    bool selectedNeighborSample = false;

    RTXDI_PTReservoir targetReservoir = RTXDI_PTSpatialResampling(srrParams, g_Const.restirPT.spatialResampling, g_Const.restirPT.hybridShift, g_Const.restirPT.reconnection, g_Const.restirPT.reservoirBuffer, g_Const.restirPT.bufferIndices, g_Const.runtimeParams, rng, selectedNeighborSample, ptud);

    if (selectedNeighborSample && !g_Const.restirPT.temporalResampling.enableAgeBasedRejection)
        targetReservoir.age = 0;

    if (selectedNeighborSample && ptud.psr.updateWithResampling && ptud.psr.hasRecorded())
    {
        UpdatePSRBuffers(ptud.psr, srrParams.pixelPosition, RTXDI_PTPathTraceInvocationType_Spatial);
    }
    if (RTXDI_PTNeedsDuplicationInputs(g_Const.restirPT.bufferIndices.spatialResamplingOutputBufferIndex, g_Const.restirPT))
        RTXDI_PTStoreDuplicationInputs(pixelPosition, targetReservoir);

    RTXDI_PTDetectDecorrelationFireflies(localIndex, targetReservoir, g_Const.restirPT.decorrelation, g_Const.restirPT.bufferIndices);

    RTXDI_StorePTReservoir(targetReservoir, g_Const.restirPT.reservoirBuffer, reservoirPosition, g_Const.restirPT.bufferIndices.spatialResamplingOutputBufferIndex);
}

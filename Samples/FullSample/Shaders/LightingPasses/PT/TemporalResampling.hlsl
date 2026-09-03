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

// Only enable the boiling filter for RayQuery (compute shader) mode because it requires shared memory
#if USE_RAY_QUERY
#define RTXDI_ENABLE_BOILING_FILTER
#define RTXDI_BOILING_FILTER_GROUP_SIZE RTXDI_SCREEN_SPACE_GROUP_SIZE
#endif

#include <Rtxdi/Utils/RandomSamplerPerPassSeeds.hlsli>

#include "Rtxdi/Utils/ReservoirAddressing.hlsli"
#include "Rtxdi/PT/BoilingFilter.hlsli"
#include "Rtxdi/PT/Reservoir.hlsli"
#include "Rtxdi/PT/TemporalResampling.hlsli"
#include "Rtxdi/PT/DuplicationMap.hlsli"
#include "Rtxdi/PT/Decorrelation.hlsli"

float3 LoadMotionVector(uint2 pixelPosition)
{
	float3 motionVector = g_Const.usePSRMvecForResampling
		? u_PSRMotionVectors[pixelPosition].xyz
		: t_MotionVectors[pixelPosition].xyz;
    motionVector = ConvertMotionVectorToPixelSpace(g_Const.view, g_Const.prevView, pixelPosition, motionVector);
	return motionVector;
}

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
#endif
    RTXDI_PTTemporalResamplingRuntimeParameters trrParams = RTXDI_EmptyPTTemporalResamplingRuntimeParameters();
    trrParams.pixelPosition = RTXDI_ReservoirPosToPixelPos(globalIndex, g_Const.runtimeParams.activeCheckerboardField);
    trrParams.reservoirPosition = RTXDI_PixelPosToReservoirPos(trrParams.pixelPosition, g_Const.runtimeParams.activeCheckerboardField);
    trrParams.motionVector = LoadMotionVector(trrParams.pixelPosition);
    trrParams.cameraPos = g_Const.view.cameraDirectionOrPosition.xyz;
    trrParams.prevCameraPos = g_Const.prevView.cameraDirectionOrPosition.xyz;
    trrParams.prevPrevCameraPos = g_Const.prevPrevView.cameraDirectionOrPosition.xyz;

	ShaderDebug::SetDebugShaderPrintCurrentThreadCursorXY(trrParams.pixelPosition);
    if (all(trrParams.pixelPosition == g_Const.debug.mouseSelectedPixel))
    {
        Debug_EnablePTPathRecording();
    }

    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(trrParams.pixelPosition, g_Const.runtimeParams.frameIndex, RTXDI_PT_TEMPORAL_RESAMPLING_RANDOM_SEED);
    RAB_PathTracerUserData ptud = RAB_EmptyPathTracerUserData();
    ptud.psr.primarySurfaceWorldPos = float3(0, 0, 0);
    ptud.psr.primarySurfaceViewDir = float3(0, 0, 0);
    ptud.psr.updateWithResampling = g_Const.updatePSRwithResampling;
    bool selectedPrevSample = false;

    RTXDI_PTReservoir Reservoir = RTXDI_PTTemporalResampling(g_Const.restirPT.temporalResampling, trrParams, g_Const.restirPT.hybridShift, g_Const.restirPT.reconnection, g_Const.runtimeParams, g_Const.restirPT.reservoirBuffer, rng, g_Const.restirPT.bufferIndices, selectedPrevSample, ptud);

    if (selectedPrevSample && ptud.psr.updateWithResampling && ptud.psr.hasRecorded() && selectedPrevSample)
    {
        UpdatePSRBuffers(ptud.psr, trrParams.pixelPosition, RTXDI_PTPathTraceInvocationType_Temporal);
    }

#ifdef RTXDI_ENABLE_BOILING_FILTER
    if (g_Const.restirPT.boilingFilter.enableBoilingFilter)
    {
        RTXDI_PTBoilingFilter(localIndex, g_Const.restirPT.boilingFilter.boilingFilterStrength, Reservoir);
    }
#endif
    if (RTXDI_PTNeedsDuplicationInputs(g_Const.restirPT.bufferIndices.temporalResamplingOutputBufferIndex, g_Const.restirPT))
        RTXDI_PTStoreDuplicationInputs(trrParams.pixelPosition, Reservoir);

    RTXDI_StorePTReservoir(Reservoir, g_Const.restirPT.reservoirBuffer, trrParams.reservoirPosition, g_Const.restirPT.bufferIndices.temporalResamplingOutputBufferIndex);
}

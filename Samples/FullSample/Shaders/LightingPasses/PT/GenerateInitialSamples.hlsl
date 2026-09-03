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
#define RTXDI_RESTIR_PT_INITIAL_SAMPLING
#include "../RtxdiApplicationBridge/PathTracer/RAB_PathTracer.hlsli"
#include "../../ShaderDebug/ShaderDebugPrint/ShaderDebugPrint.hlsli"
#include <Rtxdi/Utils/RandomSamplerPerPassSeeds.hlsli>

#include "Rtxdi/PT/InitialSampling.hlsli"
#include "Rtxdi/PT/Decorrelation.hlsli"

#if USE_RAY_QUERY
[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 globalIndex : SV_DispatchThreadID)
#else
[shader("raygeneration")]
void RayGen()
#endif
{
#if !USE_RAY_QUERY
    const uint2 globalIndex = DispatchRaysIndex().xy;
#endif
    const uint2 pixelPosition = RTXDI_ReservoirPosToPixelPos(globalIndex, g_Const.runtimeParams.activeCheckerboardField);
    const uint2 reservoirPosition = RTXDI_PixelPosToReservoirPos(pixelPosition, g_Const.runtimeParams.activeCheckerboardField);

    ShaderDebug::SetDebugShaderPrintCurrentThreadCursorXY(pixelPosition);

    RAB_Surface surface = RAB_GetGBufferSurface(pixelPosition, false);

    const bool needsNeighborSelectionGBuffer =
        g_Const.restirPT.spatialResampling.enableSpatialHeuristicMode;

    if (!RAB_IsSurfaceValid(surface))
    {
        if (needsNeighborSelectionGBuffer)
            u_NeighborSelectionGBuffer[pixelPosition] = float4(0, 0, 0, BACKGROUND_DEPTH);
    	return;
    }

	if(all(pixelPosition == g_Const.debug.mouseSelectedPixel))
    {
        Debug_EnablePTPathRecording();
        Debug_CleanPTPathRecord();
    }

    RTXDI_PathTracerRandomContext ptRandContext = RTXDI_InitializePathTracerRandomContext(pixelPosition, g_Const.runtimeParams.frameIndex, RTXDI_PT_GENERATE_INITIAL_SAMPLES_RANDOM_SEED, RTXDI_PT_GENERATE_INITIAL_SAMPLES_REPLAY_RANDOM_SEED);
    RAB_PathTracerUserData ptud = RAB_EmptyPathTracerUserData();
    ptud.psr.primarySurfaceWorldPos = surface.worldPos;
    ptud.psr.primarySurfaceViewDir = surface.viewDir;

    RTXDI_PTInitialSamplingRuntimeParameters isrParams = RTXDI_EmptyPTInitialSamplingRuntimeParameters();
    isrParams.cameraPos = g_Const.view.cameraDirectionOrPosition.xyz;
    isrParams.prevCameraPos = g_Const.prevView.cameraDirectionOrPosition.xyz;
    isrParams.prevPrevCameraPos = g_Const.prevPrevView.cameraDirectionOrPosition.xyz;

    RTXDI_PTReservoir reservoir = GenerateInitialSamples(g_Const.restirPT.initialSampling, isrParams, g_Const.restirPT.reconnection, ptRandContext, surface, ptud);

    RTXDI_StorePTReservoir(reservoir, g_Const.restirPT.reservoirBuffer, reservoirPosition, g_Const.restirPT.bufferIndices.initialPathTracerOutputBufferIndex);

    // Preserve the unresampled initial-sampling reservoir in a dedicated slot so final shading can
    // fall back to it when the decorrelation factor decides to bypass temporal/spatial resampling.
    if (RTXDI_PTNeedsPreservedInitialSample(g_Const.restirPT.decorrelation))
    {
        RTXDI_StorePTReservoir(reservoir, g_Const.restirPT.reservoirBuffer, reservoirPosition, g_Const.restirPT.bufferIndices.initialPathTracerPreservedBufferIndex);
    }

    FinalizePSRForInitialSampling(ptud.psr, reservoir);

    UpdatePSRBuffers(ptud.psr, pixelPosition, ptud.pathType);

    if (needsNeighborSelectionGBuffer)
    {
        if (g_Const.enableDenoiserPSR && ptud.psr.additionalViewZ > 0.0f)
            u_NeighborSelectionGBuffer[pixelPosition] = float4(ptud.psr.normal, ptud.psr.depth);
        else
            u_NeighborSelectionGBuffer[pixelPosition] = float4(RAB_GetSurfaceNormal(surface), surface.viewDepth);
    }
}

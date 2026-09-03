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

#ifndef PT_RESERVOIR_SUBFIELD_VIZ_HLSL
#define PT_RESERVOIR_SUBFIELD_VIZ_HLSL

#include <donut/shaders/utils.hlsli>
#include <Rtxdi/PT/ReSTIRPTParameters.h>
#include "SharedShaderInclude/ShaderParameters.h"
#include "SharedShaderInclude/ShaderDebug/ReservoirSubfieldVizPasses/PTReservoirVizParameters.h"
#include "ReservoirVizUtils.hlsli"

ConstantBuffer<PTReservoirVizParameters> g_Const : register(b0);
ConstantBuffer<ResamplingConstants> g_ConstRendering : register(b1);

RWStructuredBuffer<RTXDI_PackedPTReservoir> u_PTReservoirs : register(u0);
#define RTXDI_PT_RESERVOIR_BUFFER u_PTReservoirs

RWTexture2D<float4> t_Output : register(u1);

#include "Rtxdi/PT/Reservoir.hlsli"

[numthreads(16, 16, 1)]
void main(uint2 globalIndex : SV_DispatchThreadID)
{
    uint2 pixelPosition = RTXDI_ReservoirPosToPixelPos(globalIndex, g_Const.runtimeParams.activeCheckerboardField);

    if (any(pixelPosition > int2(g_Const.view.viewportSize)))
        return;

    const uint2 reservoirPosition = RTXDI_PixelPosToReservoirPos(pixelPosition, g_Const.runtimeParams.activeCheckerboardField);
    RTXDI_PTReservoir reservoir = RTXDI_LoadPTReservoir(g_Const.reservoirBufferParams, reservoirPosition, g_Const.bufferIndices.spatialResamplingInputBufferIndex);

    switch(g_Const.ptReservoirField)
    {
    case PT_RESERVOIR_FIELD_TRANSLATED_WORLD_POSITION:
        t_Output[pixelPosition] = float4(reservoir.translatedWorldPosition, 1.0);
    break;
    case PT_RESERVOIR_FIELD_WEIGHT_SUM:
        t_Output[pixelPosition] = reservoir.weightSum;
    break;
    case PT_RESERVOIR_FIELD_WORLD_NORMAL:
        t_Output[pixelPosition] = float4(reservoir.worldNormal, 1);
    break;
    case PT_RESERVOIR_FIELD_M:
        t_Output[pixelPosition] = RgLerp(reservoir.M / g_ConstRendering.restirPT.temporalResampling.maxHistoryLength);
    break;
	case PT_RESERVOIR_FIELD_RADIANCE:
		t_Output[pixelPosition] = float4(reservoir.radiance, 1);
	break;
    case PT_RESERVOIR_FIELD_AGE:
        t_Output[pixelPosition] = RgLerp(reservoir.age / (float)RTXDI_PTRESERVOIR_AGE_MAX);
    break;
    case PT_RESERVOIR_FIELD_RC_WI_PDF:
        t_Output[pixelPosition] = reservoir.rcWiPdf;
    break;
    case PT_RESERVOIR_FIELD_PARTIAL_JACOBIAN:
        t_Output[pixelPosition] = reservoir.partialJacobian;
    break;
    case PT_RESERVOIR_FIELD_RC_VERTEX_LENGTH:
        t_Output[pixelPosition] = reservoir.rcVertexLength;
    break;
    case PT_RESERVOIR_FIELD_PATH_LENGTH:
        t_Output[pixelPosition] = reservoir.pathLength;
    break;
    case PT_RESERVOIR_FIELD_RANDOM_SEED:
        t_Output[pixelPosition] = reservoir.randomSeed;
    break;
    case PT_RESERVOIR_FIELD_RANDOM_INDEX:
        t_Output[pixelPosition] = reservoir.randomIndex;
    break;
    case PT_RESERVOIR_FIELD_TARGET_FUNCTION:
        t_Output[pixelPosition] = float4(reservoir.targetFunction, 1.0);
    break;
    }
}

#endif // PT_RESERVOIR_SUBFIELD_VIZ_HLSL

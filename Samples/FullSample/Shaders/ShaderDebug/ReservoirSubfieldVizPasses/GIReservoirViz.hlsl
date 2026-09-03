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

#ifndef GI_RESERVOIR_SUBFIELD_VIZ_HLSL
#define GI_RESERVOIR_SUBFIELD_VIZ_HLSL

#include <donut/shaders/utils.hlsli>
#include <Rtxdi/GI/ReSTIRGIParameters.h>
#include "SharedShaderInclude/ShaderDebug/ReservoirSubfieldVizPasses/GIReservoirVizParameters.h"

ConstantBuffer<GIReservoirVizParameters> g_Const : register(b0);

RWStructuredBuffer<RTXDI_PackedGIReservoir> u_GIReservoirs : register(u0);
#define RTXDI_GI_RESERVOIR_BUFFER u_GIReservoirs

RWTexture2D<float4> t_Output : register(u1);

#include "Rtxdi/GI/Reservoir.hlsli"

[numthreads(16, 16, 1)]
void main(uint2 globalIndex : SV_DispatchThreadID)
{
    uint2 pixelPosition = RTXDI_ReservoirPosToPixelPos(globalIndex, g_Const.runtimeParams.activeCheckerboardField);

    if (any(pixelPosition > int2(g_Const.view.viewportSize)))
        return;

    const uint2 reservoirPosition = RTXDI_PixelPosToReservoirPos(pixelPosition, g_Const.runtimeParams.activeCheckerboardField);
    RTXDI_GIReservoir reservoir = RTXDI_LoadGIReservoir(g_Const.reservoirBufferParams, reservoirPosition, g_Const.bufferIndices.spatialResamplingInputBufferIndex);

    switch(g_Const.giReservoirField)
    {
    case GI_RESERVOIR_FIELD_POSITION:
        t_Output[pixelPosition] = float4(reservoir.position, 1.0);
    break;
    case GI_RESERVOIR_FIELD_NORMAL:
        t_Output[pixelPosition] = float4(reservoir.normal, 1.0);
    break;
    case GI_RESERVOIR_FIELD_RADIANCE:
        t_Output[pixelPosition] = float4(reservoir.radiance, 1);
    break;
    case GI_RESERVOIR_FIELD_WEIGHT_SUM:
        t_Output[pixelPosition] = reservoir.weightSum;
    break;
	case GI_RESERVOIR_FIELD_M:
		t_Output[pixelPosition] = reservoir.M;
	break;
    case GI_RESERVOIR_FIELD_AGE:
        t_Output[pixelPosition] = reservoir.age;
    break;
    }
}

#endif // GI_RESERVOIR_SUBFIELD_VIZ_HLSL
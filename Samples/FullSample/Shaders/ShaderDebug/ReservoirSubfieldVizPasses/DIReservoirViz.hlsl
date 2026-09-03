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

#ifndef DI_RESERVOIR_SUBFIELD_VIZ_HLSL
#define DI_RESERVOIR_SUBFIELD_VIZ_HLSL

#include <donut/shaders/utils.hlsli>
#include <Rtxdi/DI/ReSTIRDIParameters.h>
#include "SharedShaderInclude/ShaderParameters.h"
#include "SharedShaderInclude/ShaderDebug/ReservoirSubfieldVizPasses/DIReservoirVizParameters.h"
#include "ReservoirVizUtils.hlsli"

ConstantBuffer<DIReservoirVizParameters> g_Const : register(b0);
ConstantBuffer<ResamplingConstants> g_ConstRendering : register(b1);

RWStructuredBuffer<RTXDI_PackedDIReservoir> u_DIReservoirs : register(u0);
#define RTXDI_LIGHT_RESERVOIR_BUFFER u_DIReservoirs

#include <Rtxdi/DI/ReservoirStorage.hlsli>

RWTexture2D<float4> t_Output : register(u1);

#include "Rtxdi/DI/Reservoir.hlsli"

[numthreads(16, 16, 1)]
void main(uint2 globalIndex : SV_DispatchThreadID)
{
#if 1
    uint2 pixelPosition = RTXDI_ReservoirPosToPixelPos(globalIndex, g_Const.runtimeParams.activeCheckerboardField);

    if (any(pixelPosition > int2(g_Const.view.viewportSize)))
        return;

    const uint2 reservoirPosition = RTXDI_PixelPosToReservoirPos(pixelPosition, g_Const.runtimeParams.activeCheckerboardField);

    RTXDI_DIReservoir reservoir = RTXDI_LoadDIReservoir(g_Const.reservoirBufferParams, reservoirPosition, g_Const.bufferIndices.spatialResamplingInputBufferIndex);
#else
    RTXDI_DIReservoir reservoir = u_DIReservoirs[globalIndex];
#endif

    switch(g_Const.diReservoirField)
    {
    case DI_RESERVOIR_FIELD_LIGHT_DATA:
    {
        uint lightValid = reservoir.lightData & RTXDI_DIReservoir_LightValidBit;
        uint lightIndex = reservoir.lightData & RTXDI_DIReservoir_LightIndexMask;
        if (!lightValid)
        {
            t_Output[pixelPosition] = 0;
        }
        else
        {
            if (lightIndex > g_Const.maxLightsInBuffer)
                lightIndex -= g_Const.maxLightsInBuffer;
            t_Output[pixelPosition] = IndexToColor(lightIndex);
        }
    }
    break;
    case DI_RESERVOIR_FIELD_UV_DATA:
        t_Output[pixelPosition] = float4(RTXDI_GetDIReservoirSampleUV(reservoir), 0.0, 1.0);
    break;
    case DI_RESERVOIR_FIELD_TARGET_PDF:
        t_Output[pixelPosition] = reservoir.targetPdf;
    break;
    case DI_RESERVOIR_FIELD_M:
        t_Output[pixelPosition] = RgLerp(reservoir.M / (float)g_ConstRendering.restirDI.temporalResamplingParams.maxHistoryLength);
    break;
	case DI_RESERVOIR_FIELD_PACKED_VISIBILITY:
		t_Output[pixelPosition] = reservoir.packedVisibility;
	break;
	case DI_RESERVOIR_FIELD_SPATIAL_DISTANCE:
		t_Output[pixelPosition] = float4(reservoir.spatialDistance, 1.0, 1.0);
	break;
    case DI_RESERVOIR_FIELD_AGE:
        t_Output[pixelPosition] = reservoir.age;
    break;
    case DI_RESERVOIR_FIELD_CANONICAL_WEIGHT:
        t_Output[pixelPosition] = reservoir.canonicalWeight;
    break;
    }
}

#endif // DI_RESERVOIR_SUBFIELD_VIZ_HLSL
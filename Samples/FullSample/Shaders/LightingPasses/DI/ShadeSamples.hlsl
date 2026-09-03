/*
 * SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "Rtxdi/DI/Reservoir.hlsli"
#include <Rtxdi/DI/ReservoirStorage.hlsli>

#if RTXDI_REGIR_MODE != RTXDI_REGIR_DISABLED
#include "Rtxdi/ReGIR/ReGIRSampling.hlsli"
#endif

#ifdef WITH_NRD
#define NRD_HEADER_ONLY
#include <NRD.hlsli>
#endif

#include "../ShadingHelpers.hlsli"

#if USE_RAY_QUERY
[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 globalIndex : SV_DispatchThreadID, uint2 localIndex : SV_GroupThreadID, uint2 groupIdx : SV_GroupID)
#else
[shader("raygeneration")]
void RayGen()
#endif
{
#if !USE_RAY_QUERY
    uint2 globalIndex = DispatchRaysIndex().xy;
#endif

    const RTXDI_RuntimeParameters params = g_Const.runtimeParams;

    uint2 pixelPosition = RTXDI_ReservoirPosToPixelPos(globalIndex, params.activeCheckerboardField);

    RAB_Surface surface = RAB_GetGBufferSurface(pixelPosition, false);

    RTXDI_DIReservoir reservoir = RTXDI_LoadDIReservoir(g_Const.restirDI.reservoirBufferParams, globalIndex, g_Const.restirDI.bufferIndices.shadingInputBufferIndex);

    float3 diffuse = 0;
    float3 specular = 0;
    float lightDistance = 0;
    float2 currLuminance = 0;

    if (RTXDI_IsValidDIReservoir(reservoir))
    {
        RAB_LightInfo lightInfo = RAB_LoadLightInfo(RTXDI_GetDIReservoirLightIndex(reservoir), false);

        RAB_LightSample lightSample = RAB_SamplePolymorphicLight(lightInfo,
            surface, RTXDI_GetDIReservoirSampleUV(reservoir));

        bool needToStore = ShadeSurfaceWithLightSample(reservoir, surface, g_Const.restirDI.shadingParams, lightSample,
            /* prevFrameTLAS = */ false, /* enableVisibilityReuse = */ true, g_Const.restirDI.temporalResamplingParams.enableVisibilityShortcut, diffuse, specular, lightDistance);

        currLuminance = float2(CalcLuminance(diffuse * surface.material.diffuseAlbedo), CalcLuminance(specular));

        specular = DemodulateSpecular(surface.material.specularF0, specular);

        if (needToStore)
        {
            RTXDI_StoreDIReservoir(reservoir, g_Const.restirDI.reservoirBufferParams, globalIndex, g_Const.restirDI.bufferIndices.shadingInputBufferIndex);
        }
    }

    // Store the sampled lighting luminance for the gradient pass.
    // Discard the pixels where the visibility was reused, as gradients need actual visibility.
    u_RestirLuminance[globalIndex] = currLuminance * (reservoir.age > 0 ? 0 : 1);

#if RTXDI_REGIR_MODE != RTXDI_REGIR_DISABLED
    if (g_Const.visualizeRegirCells)
    {
        diffuse *= RTXDI_VisualizeReGIRCells(g_Const.regir, RAB_GetSurfaceWorldPos(surface));
    }
#endif

    StoreShadingOutput(globalIndex, pixelPosition,
        surface.viewDepth, surface.material.roughness, diffuse, specular, lightDistance, true, g_Const.restirDI.shadingParams.enableDenoiserInputPacking);

    if (g_Const.debug.outputDebugDirectLighting)
    {
        u_DirectLightingRaw[pixelPosition] = float4(diffuse + specular, 1.0);
    }
}

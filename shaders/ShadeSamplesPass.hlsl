/***************************************************************************
 # Copyright (c) 2020-2021, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma pack_matrix(row_major)

#include "RtxdiApplicationBridge.hlsli"

#include <rtxdi/ResamplingFunctions.hlsli>

#ifdef WITH_NRD
#define COMPILER_DXC
#include <NRD.hlsl>
#endif

#include "ShadingHelpers.hlsli"

#if USE_RAY_QUERY
[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 GlobalIndex : SV_DispatchThreadID)
#else
[shader("raygeneration")]
void RayGen()
#endif
{
#if !USE_RAY_QUERY
    uint2 GlobalIndex = DispatchRaysIndex().xy;
#endif

    const RTXDI_ResamplingRuntimeParameters params = g_Const.runtimeParams;

    uint2 pixelPosition = RTXDI_ReservoirToPixelPos(GlobalIndex, g_Const.runtimeParams);

    RAB_Surface surface = RAB_GetGBufferSurface(pixelPosition, false);

    RTXDI_Reservoir reservoir = RTXDI_LoadReservoir(params, u_LightReservoirs, GlobalIndex, g_Const.shadeInputBufferIndex);

    float3 diffuse = 0;
    float3 specular = 0;
    float lightDistance = 0;

    if (RTXDI_IsValidReservoir(reservoir))
    {
        RAB_LightInfo lightInfo = RAB_LoadLightInfo(RTXDI_GetReservoirLightIndex(reservoir), false);

        RAB_LightSample lightSample = RAB_SamplePolymorphicLight(lightInfo,
            surface, RTXDI_GetReservoirSampleUV(reservoir));

        bool needToStore = ShadeSurfaceWithLightSample(reservoir, surface, lightSample, diffuse, specular, lightDistance);
        specular = DemodulateSpecular(surface, specular);

        if (needToStore)
        {
            RTXDI_StoreReservoir(reservoir, params, u_LightReservoirs, GlobalIndex, g_Const.shadeInputBufferIndex);
        }
    }

    StoreRestirShadingOutput(GlobalIndex, pixelPosition, params.activeCheckerboardField, 
        surface, diffuse, specular, lightDistance);
}
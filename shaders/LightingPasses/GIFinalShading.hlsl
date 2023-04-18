/***************************************************************************
 # Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma pack_matrix(row_major)

#include "RtxdiApplicationBridge.hlsli"

#include <rtxdi/GIResamplingFunctions.hlsli>

#ifdef WITH_NRD
#define NRD_HEADER_ONLY
#include <NRDEncoding.hlsli>
#include <NRD.hlsli>
#endif

#include "ShadingHelpers.hlsli"

static const float kMaxBrdfValue = 1e4;
static const float kMISRoughness = 0.3;

float GetMISWeight(const SplitBrdf roughBrdf, const SplitBrdf trueBrdf, const float3 diffuseAlbedo)
{
    float3 combinedRoughBrdf = roughBrdf.demodulatedDiffuse * diffuseAlbedo + roughBrdf.specular;
    float3 combinedTrueBrdf = trueBrdf.demodulatedDiffuse * diffuseAlbedo + trueBrdf.specular;

    combinedRoughBrdf = clamp(combinedRoughBrdf, 1e-4, kMaxBrdfValue);
    combinedTrueBrdf = clamp(combinedTrueBrdf, 0, kMaxBrdfValue);

    const float initWeight = saturate(calcLuminance(combinedTrueBrdf) / calcLuminance(combinedTrueBrdf + combinedRoughBrdf));
    return initWeight * initWeight * initWeight;
}

RTXDI_GIReservoir LoadInitialSampleReservoir(int2 reservoirPosition, RAB_Surface primarySurface)
{
    const uint gbufferIndex = RTXDI_ReservoirPositionToPointer(g_Const.restirGI.reservoirBufferParams, reservoirPosition, 0);
    const SecondaryGBufferData secondaryGBufferData = u_SecondaryGBuffer[gbufferIndex];

    const float3 normal = octToNdirUnorm32(secondaryGBufferData.normal);
    const float3 throughput = Unpack_R16G16B16A16_FLOAT(secondaryGBufferData.throughputAndFlags).rgb;

    // Note: the secondaryGBufferData.emission field contains the sampled radiance saved in ShadeSecondarySurfaces 
    return RTXDI_MakeGIReservoir(secondaryGBufferData.worldPos,
        normal, secondaryGBufferData.emission * throughput, secondaryGBufferData.pdf);
}

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
    uint2 pixelPosition = RTXDI_ReservoirPosToPixelPos(GlobalIndex, g_Const.runtimeParams.activeCheckerboardField);

    if (any(pixelPosition > int2(g_Const.view.viewportSize)))
        return;

    const RAB_Surface primarySurface = RAB_GetGBufferSurface(pixelPosition, false);
    
    const uint2 reservoirPosition = RTXDI_PixelPosToReservoirPos(pixelPosition, g_Const.runtimeParams.activeCheckerboardField);
    const RTXDI_GIReservoir reservoir = RTXDI_LoadGIReservoir(g_Const.restirGI.reservoirBufferParams, reservoirPosition, g_Const.restirGI.bufferIndices.secondarySurfaceReSTIRDIOutputBufferIndex);
    
    float3 diffuse = 0;
    float3 specular = 0;

    if (RTXDI_IsValidGIReservoir(reservoir))
    {
        float3 radiance = reservoir.radiance * reservoir.weightSum;

        float3 visibility = 1.0;
        if (g_Const.restirGI.finalShadingParams.enableFinalVisibility)
        {
            visibility = GetFinalVisibility(SceneBVH, primarySurface, reservoir.position);
        }

        radiance *= visibility;

        const SplitBrdf brdf = EvaluateBrdf(primarySurface, reservoir.position);

        if (g_Const.restirGI.finalShadingParams.enableFinalMIS)
        {
            const RTXDI_GIReservoir initialReservoir = LoadInitialSampleReservoir(reservoirPosition, primarySurface);
            const SplitBrdf brdf0 = EvaluateBrdf(primarySurface, initialReservoir.position);

            RAB_Surface roughenedSurface = primarySurface;
            roughenedSurface.roughness = max(roughenedSurface.roughness, kMISRoughness);

            const SplitBrdf roughBrdf = EvaluateBrdf(roughenedSurface, reservoir.position);
            const SplitBrdf roughBrdf0 = EvaluateBrdf(roughenedSurface, initialReservoir.position);

            const float finalWeight = 1.0 - GetMISWeight(roughBrdf, brdf, primarySurface.diffuseAlbedo);
            const float initialWeight = GetMISWeight(roughBrdf0, brdf0, primarySurface.diffuseAlbedo);

            const float3 initialRadiance = initialReservoir.radiance * initialReservoir.weightSum;

            diffuse = brdf.demodulatedDiffuse * radiance * finalWeight 
                    + brdf0.demodulatedDiffuse * initialRadiance * initialWeight;

            specular = brdf.specular * radiance * finalWeight 
                     + brdf0.specular * initialRadiance * initialWeight;
        }
        else
        {
            diffuse = brdf.demodulatedDiffuse * radiance;
            specular = brdf.specular * radiance;
        }


        specular = DemodulateSpecular(primarySurface.specularF0, specular);
    }

    StoreShadingOutput(GlobalIndex, pixelPosition, 
        primarySurface.viewDepth, primarySurface.roughness, diffuse, specular, 0, false, true);
}

/***************************************************************************
 # Copyright (c) 2020-2022, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma pack_matrix(row_major)

// Disable specular MIS on direct lighting of the secondary surfaces,
// because we do not trace the BRDF rays further.
#define RAB_ENABLE_SPECULAR_MIS 0

#include "RtxdiApplicationBridge.hlsli"

#include <rtxdi/ResamplingFunctions.hlsli>

#ifdef WITH_NRD
#define NRD_HEADER_ONLY
#include <NRD.hlsli>
#endif

#include "ShadingHelpers.hlsli"

#ifdef WITH_RTXGI
#include "RtxgiHelpers.hlsli"
StructuredBuffer<DDGIVolumeDescGPUPacked> t_DDGIVolumes : register(t40 VK_DESCRIPTOR_SET(2));
StructuredBuffer<DDGIVolumeResourceIndices> t_DDGIVolumeResourceIndices : register(t41 VK_DESCRIPTOR_SET(2));
SamplerState s_ProbeSampler : register(s40 VK_DESCRIPTOR_SET(2));
#endif

static const float c_MaxIndirectRadiance = 10;

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
    uint2 pixelPosition = RTXDI_ReservoirToPixelPos(GlobalIndex, g_Const.runtimeParams);

    if (any(pixelPosition > int2(g_Const.view.viewportSize)))
        return;

    RAB_RandomSamplerState rng = RAB_InitRandomSampler(GlobalIndex, 6);
    RAB_RandomSamplerState tileRng = RAB_InitRandomSampler(GlobalIndex / RTXDI_TILE_SIZE_IN_PIXELS, 1);

    const RTXDI_ResamplingRuntimeParameters params = g_Const.runtimeParams;
    uint gbufferIndex = RTXDI_ReservoirPositionToPointer(params, GlobalIndex, 0);

    RAB_Surface primarySurface = RAB_GetGBufferSurface(pixelPosition, false);

    SecondarySurface secondarySurface = u_SecondaryGBuffer[gbufferIndex];

    float3 diffuse = 0;
    float3 specular = 0;

    if (any(secondarySurface.throughput != 0))
    {
        RAB_Surface surface;
        surface.worldPos = secondarySurface.worldPos;
        surface.viewDepth = 1.0; // doesn't matter
        surface.normal = octToNdirUnorm32(secondarySurface.normal);
        surface.geoNormal = surface.normal;
        surface.diffuseAlbedo = Unpack_R11G11B10_UFLOAT(secondarySurface.diffuseAlbedo);
        float4 specularRough = Unpack_R8G8B8A8_Gamma_UFLOAT(secondarySurface.specularAndRoughness);
        surface.specularF0 = specularRough.rgb;
        surface.roughness = specularRough.a;
        surface.diffuseProbability = getSurfaceDiffuseProbability(surface);
        surface.viewDir = normalize(primarySurface.worldPos - surface.worldPos);

        float4 throughput = Unpack_R16G16B16A16_FLOAT(secondarySurface.throughput);
        bool isSpecularRay = throughput.a != 0;

        RTXDI_SampleParameters sampleParams = RTXDI_InitSampleParameters(
            g_Const.numIndirectRegirSamples,
            g_Const.numIndirectLocalLightSamples,
            g_Const.numIndirectInfiniteLightSamples,
            g_Const.numIndirectEnvironmentSamples,
            0,      // numBrdfSamples
            0.f,    // brdfCutoff 
            0.f);   // brdfMinRayT

        RAB_LightSample lightSample;
        RTXDI_Reservoir reservoir = RTXDI_SampleLightsForSurface(rng, tileRng, surface,
            sampleParams, params, lightSample);

        if (g_Const.numSecondarySamples)
        {
            // Try to find this secondary surface in the G-buffer. If found, resample the lights
            // from that G-buffer surface into the reservoir using the spatial resampling function.

            float4 secondaryClipPos = mul(float4(secondarySurface.worldPos, 1.0), g_Const.view.matWorldToClip);
            secondaryClipPos.xyz /= secondaryClipPos.w;
            
            if (all(abs(secondaryClipPos.xy) < 1.0) && secondaryClipPos.w > 0)
            {
                int2 secondaryPixelPos = int2(secondaryClipPos.xy * g_Const.view.clipToWindowScale + g_Const.view.clipToWindowBias);
                surface.viewDepth = secondaryClipPos.w;

                RTXDI_SpatialResamplingParameters sparams;
                sparams.sourceBufferIndex = g_Const.shadeInputBufferIndex;
                sparams.numSamples = g_Const.numSecondarySamples;
                sparams.numDisocclusionBoostSamples = 0;
                sparams.targetHistoryLength = 0;
                sparams.biasCorrectionMode = g_Const.secondaryBiasCorrection;
                sparams.samplingRadius = g_Const.secondarySamplingRadius;
                sparams.depthThreshold = g_Const.secondaryDepthThreshold;
                sparams.normalThreshold = g_Const.secondaryNormalThreshold;

                reservoir = RTXDI_SpatialResampling(secondaryPixelPos, surface, reservoir,
                    rng, sparams,params, lightSample);
            }
        }

        float lightSampleScale = (lightSample.solidAnglePdf > 0) ? RTXDI_GetReservoirInvPdf(reservoir) / lightSample.solidAnglePdf : 0;

        // Firefly suppression
        float indirectLuminance = calcLuminance(lightSample.radiance) * lightSampleScale;
        if(indirectLuminance > c_MaxIndirectRadiance)
            lightSampleScale *= c_MaxIndirectRadiance / indirectLuminance;

        float3 indirectDiffuse = 0;
        float3 indirectSpecular = 0;
        float lightDistance = 0;
        ShadeSurfaceWithLightSample(reservoir, surface, lightSample, /* previousFrameTLAS = */ false, 
            /* enableVisibilityReuse = */ false, indirectDiffuse, indirectSpecular, lightDistance);
        
        float3 radiance = indirectDiffuse * surface.diffuseAlbedo + indirectSpecular;

#ifdef WITH_RTXGI
        if (g_Const.numRtxgiVolumes)
        {
            float3 indirectIrradiance = GetIrradianceFromDDGI(
                surface.worldPos,
                surface.normal,
                primarySurface.worldPos,
                g_Const.numRtxgiVolumes,
                t_DDGIVolumes,
                t_DDGIVolumeResourceIndices,
                s_ProbeSampler);

            radiance += indirectIrradiance * surface.diffuseAlbedo;
        }
#endif

        radiance *= throughput.rgb;

        diffuse = isSpecularRay ? 0.0 : radiance;
        specular = isSpecularRay ? radiance : 0.0;
    }

    specular = DemodulateSpecular(primarySurface.specularF0, specular);

    StoreShadingOutput(GlobalIndex, pixelPosition, 
        primarySurface.viewDepth, primarySurface.roughness, diffuse, specular, 0, false, true);
}

/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
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

#include <rtxdi/InitialSamplingFunctions.hlsli>
#include <rtxdi/DIResamplingFunctions.hlsli>
#include <rtxdi/GIResamplingFunctions.hlsli>

#ifdef WITH_NRD
#define NRD_HEADER_ONLY
#include <NRDEncoding.hlsli>
#include <NRD.hlsli>
#endif

#include "ShadingHelpers.hlsli"

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
    uint2 pixelPosition = RTXDI_ReservoirPosToPixelPos(GlobalIndex, g_Const.runtimeParams.activeCheckerboardField);

    if (any(pixelPosition > int2(g_Const.view.viewportSize)))
        return;

    RAB_RandomSamplerState rng = RAB_InitRandomSampler(GlobalIndex, 6);
    RAB_RandomSamplerState tileRng = RAB_InitRandomSampler(GlobalIndex / RTXDI_TILE_SIZE_IN_PIXELS, 1);

    const RTXDI_RuntimeParameters params = g_Const.runtimeParams;
    const uint gbufferIndex = RTXDI_ReservoirPositionToPointer(g_Const.restirDI.reservoirBufferParams, GlobalIndex, 0);

    RAB_Surface primarySurface = RAB_GetGBufferSurface(pixelPosition, false);

    SecondaryGBufferData secondaryGBufferData = u_SecondaryGBuffer[gbufferIndex];

    const float3 throughput = Unpack_R16G16B16A16_FLOAT(secondaryGBufferData.throughputAndFlags).rgb;
    const uint secondaryFlags = secondaryGBufferData.throughputAndFlags.y >> 16;
    const bool isValidSecondarySurface = any(throughput != 0);
    const bool isSpecularRay = (secondaryFlags & kSecondaryGBuffer_IsSpecularRay) != 0;
    const bool isDeltaSurface = (secondaryFlags & kSecondaryGBuffer_IsDeltaSurface) != 0;
    const bool isEnvironmentMap = (secondaryFlags & kSecondaryGBuffer_IsEnvironmentMap) != 0;

    RAB_Surface secondarySurface;
    float3 radiance = secondaryGBufferData.emission;

    // Unpack the G-buffer data
    secondarySurface.worldPos = secondaryGBufferData.worldPos;
    secondarySurface.viewDepth = 1.0; // doesn't matter
    secondarySurface.normal = octToNdirUnorm32(secondaryGBufferData.normal);
    secondarySurface.geoNormal = secondarySurface.normal;
    secondarySurface.diffuseAlbedo = Unpack_R11G11B10_UFLOAT(secondaryGBufferData.diffuseAlbedo);
    float4 specularRough = Unpack_R8G8B8A8_Gamma_UFLOAT(secondaryGBufferData.specularAndRoughness);
    secondarySurface.specularF0 = specularRough.rgb;
    secondarySurface.roughness = specularRough.a;
    secondarySurface.diffuseProbability = getSurfaceDiffuseProbability(secondarySurface);
    secondarySurface.viewDir = normalize(primarySurface.worldPos - secondarySurface.worldPos);

    // Shade the secondary surface.
    if (isValidSecondarySurface && !isEnvironmentMap)
    {
        RTXDI_SampleParameters sampleParams = RTXDI_InitSampleParameters(
            g_Const.brdfPT.secondarySurfaceReSTIRDIParams.initialSamplingParams.numPrimaryLocalLightSamples,
            g_Const.brdfPT.secondarySurfaceReSTIRDIParams.initialSamplingParams.numPrimaryInfiniteLightSamples,
            g_Const.brdfPT.secondarySurfaceReSTIRDIParams.initialSamplingParams.numPrimaryEnvironmentSamples,
            0,      // numBrdfSamples
            0.f,    // brdfCutoff 
            0.f);   // brdfMinRayT

        RAB_LightSample lightSample;
        RTXDI_DIReservoir reservoir = RTXDI_SampleLightsForSurface(rng, tileRng, secondarySurface,
            sampleParams, g_Const.lightBufferParams, g_Const.brdfPT.secondarySurfaceReSTIRDIParams.initialSamplingParams.localLightSamplingMode,
#if RTXDI_ENABLE_PRESAMPLING
        g_Const.localLightsRISBufferSegmentParams, g_Const.environmentLightRISBufferSegmentParams,
#if RTXDI_REGIR_MODE != RTXDI_REGIR_MODE_DISABLED
        g_Const.regir,
#endif
#endif
        lightSample);

        if (g_Const.brdfPT.enableSecondaryResampling)
        {
            // Try to find this secondary surface in the G-buffer. If found, resample the lights
            // from that G-buffer surface into the reservoir using the spatial resampling function.

            float4 secondaryClipPos = mul(float4(secondaryGBufferData.worldPos, 1.0), g_Const.view.matWorldToClip);
            secondaryClipPos.xyz /= secondaryClipPos.w;

            if (all(abs(secondaryClipPos.xy) < 1.0) && secondaryClipPos.w > 0)
            {
                int2 secondaryPixelPos = int2(secondaryClipPos.xy * g_Const.view.clipToWindowScale + g_Const.view.clipToWindowBias);
                secondarySurface.viewDepth = secondaryClipPos.w;

                RTXDI_DISpatialResamplingParameters sparams;
                sparams.sourceBufferIndex = g_Const.restirDI.bufferIndices.shadingInputBufferIndex;
                sparams.numSamples = g_Const.brdfPT.secondarySurfaceReSTIRDIParams.spatialResamplingParams.numSpatialSamples;
                sparams.numDisocclusionBoostSamples = 0;
                sparams.targetHistoryLength = 0;
                sparams.biasCorrectionMode = g_Const.brdfPT.secondarySurfaceReSTIRDIParams.spatialResamplingParams.spatialBiasCorrection;
                sparams.samplingRadius = g_Const.brdfPT.secondarySurfaceReSTIRDIParams.spatialResamplingParams.spatialSamplingRadius;
                sparams.depthThreshold = g_Const.brdfPT.secondarySurfaceReSTIRDIParams.spatialResamplingParams.spatialDepthThreshold;
                sparams.normalThreshold = g_Const.brdfPT.secondarySurfaceReSTIRDIParams.spatialResamplingParams.spatialNormalThreshold;
                sparams.enableMaterialSimilarityTest = false;
                sparams.discountNaiveSamples = false;

                reservoir = RTXDI_DISpatialResampling(secondaryPixelPos, secondarySurface, reservoir,
                    rng, params, g_Const.restirDI.reservoirBufferParams, sparams, lightSample);
            }
        }

        float3 indirectDiffuse = 0;
        float3 indirectSpecular = 0;
        float lightDistance = 0;
        ShadeSurfaceWithLightSample(reservoir, secondarySurface, lightSample, /* previousFrameTLAS = */ false,
            /* enableVisibilityReuse = */ false, indirectDiffuse, indirectSpecular, lightDistance);

        radiance += indirectDiffuse * secondarySurface.diffuseAlbedo + indirectSpecular;

        // Firefly suppression
        float indirectLuminance = calcLuminance(radiance);
        if (indirectLuminance > c_MaxIndirectRadiance)
            radiance *= c_MaxIndirectRadiance / indirectLuminance;
    }

    bool outputShadingResult = true;
    if (g_Const.brdfPT.enableReSTIRGI)
    {
        RTXDI_GIReservoir reservoir = RTXDI_EmptyGIReservoir();

        // For delta reflection rays, just output the shading result in this shader
        // and don't include it into ReSTIR GI reservoirs.
        outputShadingResult = isSpecularRay && isDeltaSurface;

        if (isValidSecondarySurface && !outputShadingResult)
        {
            // This pixel has a valid indirect sample so it stores information as an initial GI reservoir.
            reservoir = RTXDI_MakeGIReservoir(secondarySurface.worldPos,
                secondarySurface.normal, radiance, secondaryGBufferData.pdf);
        }
        uint2 reservoirPosition = RTXDI_PixelPosToReservoirPos(pixelPosition, g_Const.runtimeParams.activeCheckerboardField);
        RTXDI_StoreGIReservoir(reservoir, g_Const.restirGI.reservoirBufferParams, reservoirPosition, g_Const.restirGI.bufferIndices.secondarySurfaceReSTIRDIOutputBufferIndex);

        // Save the initial sample radiance for MIS in the final shading pass
        secondaryGBufferData.emission = outputShadingResult ? 0 : radiance;
        u_SecondaryGBuffer[gbufferIndex] = secondaryGBufferData;
    }

    if (outputShadingResult)
    {
        float3 diffuse = isSpecularRay ? 0.0 : radiance * throughput.rgb;
        float3 specular = isSpecularRay ? radiance * throughput.rgb : 0.0;

        specular = DemodulateSpecular(primarySurface.specularF0, specular);

        StoreShadingOutput(GlobalIndex, pixelPosition, 
            primarySurface.viewDepth, primarySurface.roughness, diffuse, specular, 0, false, true);
    }
}

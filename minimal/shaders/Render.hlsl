/***************************************************************************
 # Copyright (c) 2021-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma pack_matrix(row_major)

#define RTXDI_ENABLE_PRESAMPLING 0
#include "RtxdiApplicationBridge.hlsli"

#include <rtxdi/InitialSamplingFunctions.hlsli>
#include <rtxdi/DIResamplingFunctions.hlsli>

#include "PrimaryRays.hlsli"

[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 pixelPosition : SV_DispatchThreadID)
{
    const RTXDI_LightBufferParameters lightBufferParams = g_Const.lightBufferParams;

    // Trace the primary ray
    PrimarySurfaceOutput primary = TracePrimaryRay(pixelPosition);

    // Store the G-buffer data for resampling on the next frame
    u_GBufferDepth[pixelPosition] = primary.surface.viewDepth;
    u_GBufferNormals[pixelPosition] = ndirToOctUnorm32(primary.surface.normal);
    u_GBufferGeoNormals[pixelPosition] = ndirToOctUnorm32(primary.surface.geoNormal);
    u_GBufferDiffuseAlbedo[pixelPosition] = Pack_R11G11B10_UFLOAT(primary.surface.diffuseAlbedo);
    u_GBufferSpecularRough[pixelPosition] = Pack_R8G8B8A8_Gamma_UFLOAT(float4(primary.surface.specularF0, primary.surface.roughness));
    
    RTXDI_DIReservoir reservoir = RTXDI_EmptyDIReservoir();

    if (RAB_IsSurfaceValid(primary.surface))
    {
        // Initialize the RNG
        RAB_RandomSamplerState rng = RAB_InitRandomSampler(pixelPosition, 1);
        
        RTXDI_SampleParameters sampleParams = RTXDI_InitSampleParameters(
            g_Const.numInitialSamples, // local light samples 
            0, // infinite light samples
            0, // environment map samples
            g_Const.numInitialBRDFSamples,
            g_Const.brdfCutoff,
            0.001f);

        // Generate the initial sample
        RAB_LightSample lightSample = RAB_EmptyLightSample();
        RTXDI_DIReservoir localReservoir = RTXDI_SampleLocalLights(rng, rng, primary.surface,
            sampleParams, ReSTIRDI_LocalLightSamplingMode_UNIFORM, lightBufferParams.localLightBufferRegion, lightSample);
        RTXDI_CombineDIReservoirs(reservoir, localReservoir, 0.5, localReservoir.targetPdf);

        // Resample BRDF samples.
        RAB_LightSample brdfSample = RAB_EmptyLightSample();
        RTXDI_DIReservoir brdfReservoir = RTXDI_SampleBrdf(rng, primary.surface, sampleParams, lightBufferParams, brdfSample);
        bool selectBrdf = RTXDI_CombineDIReservoirs(reservoir, brdfReservoir, RAB_GetNextRandom(rng), brdfReservoir.targetPdf);
        if (selectBrdf)
        {
            lightSample = brdfSample;
        }

        RTXDI_FinalizeResampling(reservoir, 1.0, 1.0);
        reservoir.M = 1;
         
        // BRDF was generated with a trace so no need to trace visibility again
        if (RTXDI_IsValidDIReservoir(reservoir) && !selectBrdf)
        {
            // See if the initial sample is visible from the surface
            if (!RAB_GetConservativeVisibility(primary.surface, lightSample))
            {
                // If not visible, discard the sample (but keep the M)
                RTXDI_StoreVisibilityInDIReservoir(reservoir, 0, true);
            }
        }

        // Apply spatio-temporal resampling, if enabled
        if (g_Const.enableResampling)
        {
            // Fill out the parameter structure.
            // Mostly use literal constants for simplicity.
            RTXDI_DISpatioTemporalResamplingParameters stparams;
            stparams.screenSpaceMotion = primary.motionVector;
            stparams.sourceBufferIndex = g_Const.inputBufferIndex;
            stparams.maxHistoryLength = 20;
            stparams.biasCorrectionMode = g_Const.unbiasedMode ? RTXDI_BIAS_CORRECTION_RAY_TRACED : RTXDI_BIAS_CORRECTION_BASIC;
            stparams.depthThreshold = 0.1;
            stparams.normalThreshold = 0.5;
            stparams.numSamples = g_Const.numSpatialSamples + 1;
            stparams.numDisocclusionBoostSamples = 0;
            stparams.samplingRadius = 32;
            stparams.enableVisibilityShortcut = true;
            stparams.enablePermutationSampling = true;
            stparams.discountNaiveSamples = false;

            // This variable will receive the position of the sample reused from the previous frame.
            // It's only needed for gradient evaluation, ignore it here.
            int2 temporalSamplePixelPos = -1;

            // Call the resampling function, update the reservoir and lightSample variables
            reservoir = RTXDI_DISpatioTemporalResampling(pixelPosition, primary.surface, reservoir,
                    rng, g_Const.runtimeParams, g_Const.restirDIReservoirBufferParams, stparams, temporalSamplePixelPos, lightSample);
        }

        float3 shadingOutput = 0;

        // Shade the surface with the selected light sample
        if (RTXDI_IsValidDIReservoir(reservoir))
        {
            // Compute the correctly weighted reflected radiance
            shadingOutput = ShadeSurfaceWithLightSample(lightSample, primary.surface)
                          * RTXDI_GetDIReservoirInvPdf(reservoir);

            // Test if the selected light is visible from the surface
            bool visibility = RAB_GetConservativeVisibility(primary.surface, lightSample);

            // If not visible, discard the shading output and the light sample
            if (!visibility)
            {
                shadingOutput = 0;
                RTXDI_StoreVisibilityInDIReservoir(reservoir, 0, true);
            }
        }

        // Compositing and tone mapping
        shadingOutput += primary.emissiveColor;
        shadingOutput = basicToneMapping(shadingOutput, 0.005);

        u_ShadingOutput[pixelPosition] = float4(shadingOutput, 0);
    }
    else
    {
        // No valid surface
        u_ShadingOutput[pixelPosition] = 0;
    }

    RTXDI_StoreDIReservoir(reservoir, g_Const.restirDIReservoirBufferParams, pixelPosition, g_Const.outputBufferIndex);
}
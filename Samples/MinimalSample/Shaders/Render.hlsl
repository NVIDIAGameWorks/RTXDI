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
#include "RtxdiApplicationBridge/RtxdiApplicationBridge.hlsli"

#include <Rtxdi/DI/InitialSampling.hlsli>
#include <Rtxdi/DI/ReservoirStorage.hlsli>
#include <Rtxdi/DI/SpatioTemporalResampling.hlsli>

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
    u_GBufferDiffuseAlbedo[pixelPosition] = Pack_R11G11B10_UFLOAT(primary.surface.material.diffuseAlbedo);
    u_GBufferSpecularRough[pixelPosition] = Pack_R8G8B8A8_Gamma_UFLOAT(float4(primary.surface.material.specularF0, primary.surface.material.roughness));
    
    RTXDI_DIReservoir reservoir = RTXDI_EmptyDIReservoir();

    if (RAB_IsSurfaceValid(primary.surface))
    {
        // Initialize the RNG
        RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(pixelPosition, g_Const.runtimeParams.frameIndex, 1);

		RTXDI_InitialSamplingMisData misData = RTXDI_ComputeInitialSamplingMisData(g_Const.initialSamplingParams);
        
        // Generate the initial sample
        RAB_LightSample lightSample = RAB_EmptyLightSample();
        RTXDI_DIReservoir localReservoir = RTXDI_SampleLocalLights(rng, rng, primary.surface,
            g_Const.initialSamplingParams, misData, ReSTIRDI_LocalLightSamplingMode_UNIFORM, lightBufferParams.localLightBufferRegion, lightSample);
        RTXDI_CombineDIReservoirs(reservoir, localReservoir, 0.5, localReservoir.targetPdf);

        // Resample BRDF samples.
        RAB_LightSample brdfSample = RAB_EmptyLightSample();
        RTXDI_DIReservoir brdfReservoir = RTXDI_SampleBrdf(rng, primary.surface, g_Const.initialSamplingParams.numBrdfSamples, g_Const.initialSamplingParams.brdfCutoff, 0.001f, misData, rng, lightBufferParams, brdfSample);
        bool selectBrdf = RTXDI_CombineDIReservoirs(reservoir, brdfReservoir, RTXDI_GetNextRandom(rng), brdfReservoir.targetPdf);
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
            // This variable will receive the position of the sample reused from the previous frame.
            // It's only needed for gradient evaluation, ignore it here.
            int2 temporalSamplePixelPos = -1;

            // Call the resampling function, update the reservoir and lightSample variables
            reservoir = RTXDI_DISpatioTemporalResampling(pixelPosition, primary.surface, reservoir,
                    rng, primary.motionVector, g_Const.inputBufferIndex, g_Const.runtimeParams, g_Const.restirDIReservoirBufferParams, g_Const.spatioTemporalResamplingParams, temporalSamplePixelPos, lightSample);
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
        shadingOutput = BasicToneMapping(shadingOutput, 0.005);

        u_ShadingOutput[pixelPosition] = float4(shadingOutput, 0);
    }
    else
    {
        // No valid surface
        u_ShadingOutput[pixelPosition] = 0;
    }

    RTXDI_StoreDIReservoir(reservoir, g_Const.restirDIReservoirBufferParams, pixelPosition, g_Const.outputBufferIndex);
}
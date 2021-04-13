/***************************************************************************
 # Copyright (c) 2021, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#ifndef SHADING_HELPERS_HLSLI
#define SHADING_HELPERS_HLSLI

bool ShadeSurfaceWithLightSample(
    inout RTXDI_Reservoir reservoir,
    RAB_Surface surface,
    RAB_LightSample lightSample,
    out float3 diffuse,
    out float3 specular,
    out float lightDistance)
{
    diffuse = 0;
    specular = 0;
    lightDistance = 0;

    if (lightSample.solidAnglePdf <= 0)
        return false;

    bool needToStore = false;
    if (g_Const.enableFinalVisibility)
    {
        float3 visibility = 0;
        bool visibilityReused = false;

        if (g_Const.reuseFinalVisibility)
        {
            RTXDI_VisibilityReuseParameters rparams;
            rparams.maxAge = g_Const.finalVisibilityMaxAge;
            rparams.maxDistance = g_Const.finalVisibilityMaxDistance;

            visibilityReused = RTXDI_GetReservoirVisibility(reservoir, rparams, visibility);
        }

        if (!visibilityReused)
        {
            visibility = GetFinalVisibility(surface, lightSample);
            RTXDI_StoreVisibilityInReservoir(reservoir, visibility, g_Const.discardInvisibleSamples);
            needToStore = true;
        }

        lightSample.radiance *= visibility;
    }

    lightSample.radiance *= RTXDI_GetReservoirInvPdf(reservoir) / lightSample.solidAnglePdf;

    if (any(lightSample.radiance > 0))
    {
        float3 L = lightSample.position - RAB_GetSurfaceWorldPos(surface);
        lightDistance = length(L);
        L /= lightDistance;

        float3 V = normalize(g_Const.view.cameraDirectionOrPosition.xyz - surface.worldPos);

        float F0 = lerp(0.05, 1.0, surface.metalness);
        float d = Lambert(surface.normal, -L);
        float s = GGX(V, L, surface.normal, surface.roughness, F0);

        s *= EvaluateSpecularSampledLightingWeight(surface, L, lightSample.solidAnglePdf);

        diffuse = d * lightSample.radiance;
        specular = s * lightSample.radiance;
    }

    return needToStore;
}


void StoreRestirShadingOutput(
    uint2 GlobalIndex,
    uint2 pixelPosition,
    uint activeCheckerboardField,
    RAB_Surface surface,
    float3 diffuse,
    float3 specular,
    float lightDistance)
{

#if RTXDI_REGIR_MODE != RTXDI_REGIR_DISABLED
    if (g_Const.visualizeRegirCells)
    {
        diffuse *= RTXDI_VisualizeReGIRCells(g_Const.runtimeParams, RAB_GetSurfaceWorldPos(surface));
    }
#endif
  
    uint2 lightingTexturePos = (g_Const.denoiserMode == DENOISER_MODE_REBLUR)
        ? GlobalIndex
        : pixelPosition;

    if (g_Const.denoiserMode != DENOISER_MODE_REBLUR && activeCheckerboardField != 0)
    {
        diffuse *= 2;
        specular *= 2;

        int2 otherFieldPixelPosition = pixelPosition;
        otherFieldPixelPosition.x += (activeCheckerboardField == 1) == ((pixelPosition.y & 1) != 0)
            ? 1 : -1;

        u_DiffuseLighting[otherFieldPixelPosition] = 0;
        u_SpecularLighting[otherFieldPixelPosition] = 0;
    }

#if WITH_NRD
    if(g_Const.denoiserMode != DENOISER_MODE_OFF && g_Const.enableDenoiserInputPacking)
    {
        const bool useReLAX = (g_Const.denoiserMode == DENOISER_MODE_RELAX);
 
        if (useReLAX)
        {
            u_DiffuseLighting[lightingTexturePos] = RELAX_FrontEnd_PackRadiance(diffuse, lightDistance);
            u_SpecularLighting[lightingTexturePos] = RELAX_FrontEnd_PackRadiance(specular, lightDistance, surface.roughness);
        }
        else
        {
            float diffNormDist = REBLUR_FrontEnd_GetNormHitDist(lightDistance, surface.viewDepth, g_Const.reblurDiffHitDistParams);
            u_DiffuseLighting[lightingTexturePos] = REBLUR_FrontEnd_PackRadiance(diffuse, diffNormDist, surface.viewDepth, g_Const.reblurDiffHitDistParams);
            
            float specNormDist = REBLUR_FrontEnd_GetNormHitDist(lightDistance, surface.viewDepth, g_Const.reblurSpecHitDistParams, surface.roughness);
            u_SpecularLighting[lightingTexturePos] = REBLUR_FrontEnd_PackRadiance(specular, specNormDist, surface.viewDepth, g_Const.reblurSpecHitDistParams, surface.roughness);
        }
    }
    else
#endif
    {
        u_DiffuseLighting[lightingTexturePos] = float4(diffuse, lightDistance);
        u_SpecularLighting[lightingTexturePos] = float4(specular, lightDistance);
    }
}

#endif // SHADING_HELPERS_HLSLI
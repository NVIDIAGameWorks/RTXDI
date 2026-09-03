/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#ifndef SHADING_HELPERS_HLSLI
#define SHADING_HELPERS_HLSLI

#include "Rtxdi/DI/Reservoir.hlsli"

#ifdef WITH_NRD
#include <NRD.hlsli>
#endif

struct SplitBrdf
{
    float demodulatedDiffuse;
    float3 specular;
};

// TODO: Fix implementation of this new custom function
SplitBrdf EvaluateBrdfPT(RAB_Surface surface, float3 N, float3 V, float3 L)
{
	SplitBrdf brdf = (SplitBrdf)0;
    brdf.demodulatedDiffuse = Lambert(surface.normal, -L);
    brdf.specular = GGX_times_NdotL(V, L, surface.normal, max(surface.material.roughness, kMinRoughness), surface.material.specularF0);
    return brdf;
}

SplitBrdf EvaluateBrdf(RAB_Surface surface, float3 samplePosition)
{
    float3 N = surface.normal;
    float3 V = surface.viewDir;
    float3 L = normalize(samplePosition - surface.worldPos);

    SplitBrdf brdf;
    brdf.demodulatedDiffuse = Lambert(surface.normal, -L);
    if (surface.material.roughness < kMinRoughness)
        brdf.specular = 0;
    else
        brdf.specular = GGX_times_NdotL(V, L, surface.normal, max(surface.material.roughness, kMinRoughness), surface.material.specularF0);
    return brdf;
}

#ifdef RTXDI_DIRESERVOIR_HLSLI

bool ShadeSurfaceWithLightSample(
    inout RTXDI_DIReservoir reservoir,
    RAB_Surface surface,
	RTXDI_ShadingParameters shadingParams,
    RAB_LightSample lightSample,
    bool prevFrameTLAS,
    bool enableVisibilityReuse,
    bool enableVisibilityShortcut,
    out float3 diffuse,
    out float3 specular,
    out float lightDistance)
{
    diffuse = float3(0.0f, 0.0f, 0.0f);
    specular = float3(0.0f, 0.0f, 0.0f);
    lightDistance = 0.0f;

    if (lightSample.solidAnglePdf <= 0)
        return false;

    bool needToStore = false;
	// TODO: This final visibility check always seems to destroy the reservoir.
    if (shadingParams.enableFinalVisibility)
    {
        float3 visibility = float3(0.0f, 0.0f, 0.0f);
        bool visibilityReused = false;

        if (shadingParams.reuseFinalVisibility && enableVisibilityReuse)
        {
            RTXDI_VisibilityReuseParameters rparams;
            rparams.maxAge = shadingParams.finalVisibilityMaxAge;
            rparams.maxDistance = shadingParams.finalVisibilityMaxDistance;

            visibilityReused = RTXDI_GetDIReservoirVisibility(reservoir, rparams, visibility);
        }

        if (!visibilityReused)
        {
            if (prevFrameTLAS && g_Const.enablePrevTLAS)
                visibility = GetFinalVisibility(PrevSceneBVH, surface, lightSample.position);
            else
                visibility = GetFinalVisibility(SceneBVH, surface, lightSample.position);
            RTXDI_StoreVisibilityInDIReservoir(reservoir, visibility, enableVisibilityShortcut);
            needToStore = true;
        }

        lightSample.radiance *= visibility;
    }

    lightSample.radiance *= RTXDI_GetDIReservoirInvPdf(reservoir) / lightSample.solidAnglePdf;

    if (any(lightSample.radiance > 0))
    {
        SplitBrdf brdf = EvaluateBrdf(surface, lightSample.position);

        diffuse = brdf.demodulatedDiffuse * lightSample.radiance;
        specular = brdf.specular * lightSample.radiance;

        lightDistance = length(lightSample.position - surface.worldPos);
    }

    return needToStore;
}

#endif // RTXDI_DIRESERVOIR_HLSLI

float3 DemodulateSpecular(float3 surfaceSpecularF0, float3 specular)
{
    return specular / max(0.01, surfaceSpecularF0);
}


void StoreShadingOutput(
    uint2 reservoirPosition,
    uint2 pixelPosition,
    float viewDepth,
    float roughness,
    float3 diffuse,
    float3 specular,
    float lightDistance,
    bool isFirstPass,
    bool isLastPass)
{
    uint2 lightingTexturePos = (g_Const.denoiserMode != DENOISER_MODE_OFF)
        ? reservoirPosition
        : pixelPosition;

    float diffuseHitT = lightDistance;
    float specularHitT = lightDistance;

    if (!isFirstPass)
    {
        float4 priorDiffuse = u_DiffuseLighting[lightingTexturePos];
        float4 priorSpecular = u_SpecularLighting[lightingTexturePos];

        if (CalcLuminance(diffuse) > CalcLuminance(priorDiffuse.rgb) || lightDistance == 0)
            diffuseHitT = priorDiffuse.w;

        if (CalcLuminance(specular) > CalcLuminance(priorSpecular.rgb) || lightDistance == 0)
            specularHitT = priorSpecular.w;

        diffuse += priorDiffuse.rgb;
        specular += priorSpecular.rgb;
    }

    if (g_Const.denoiserMode == DENOISER_MODE_OFF && g_Const.runtimeParams.activeCheckerboardField != 0 && isLastPass)
    {
        int2 otherFieldPixelPosition = pixelPosition;
        otherFieldPixelPosition.x += (g_Const.runtimeParams.activeCheckerboardField == 1) == ((pixelPosition.y & 1) != 0)
            ? 1 : -1;

        if (g_Const.denoiserMode == DENOISER_MODE_RELAX || g_Const.enableAccumulation)
        {
            diffuse *= 2;
            specular *= 2;

            u_DiffuseLighting[otherFieldPixelPosition] = 0;
            u_SpecularLighting[otherFieldPixelPosition] = 0;
        }
        else // g_Const.denoiserMode == DENOISER_MODE_OFF
        {
            u_DiffuseLighting[otherFieldPixelPosition] = float4(diffuse, 0);
            u_SpecularLighting[otherFieldPixelPosition] = float4(specular, 0);
        }
    }

#if WITH_NRD
    if(g_Const.denoiserMode != DENOISER_MODE_OFF && isLastPass)
    {
        const bool useReLAX = (g_Const.denoiserMode == DENOISER_MODE_RELAX);
        const bool sanitize = true;

        if (useReLAX)
        {
            u_DiffuseLighting[lightingTexturePos] = RELAX_FrontEnd_PackRadianceAndHitDist(diffuse, diffuseHitT, sanitize);
            u_SpecularLighting[lightingTexturePos] = RELAX_FrontEnd_PackRadianceAndHitDist(specular, specularHitT, sanitize);
        }
        else
        {
            float diffNormDist = REBLUR_FrontEnd_GetNormHitDist(diffuseHitT, viewDepth, g_Const.reblurHitDistParams, 1.0);
            u_DiffuseLighting[lightingTexturePos] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(diffuse, diffNormDist, sanitize);

            float specNormDist = REBLUR_FrontEnd_GetNormHitDist(specularHitT, viewDepth, g_Const.reblurHitDistParams, roughness);
            u_SpecularLighting[lightingTexturePos] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(specular, specNormDist, sanitize);
        }
    }
    else
#endif
    {
        u_DiffuseLighting[lightingTexturePos] = float4(diffuse, diffuseHitT);
        u_SpecularLighting[lightingTexturePos] = float4(specular, specularHitT);
    }
}

// Variant that allows per-lobe hitT
void StoreShadingOutput(
    uint2 reservoirPosition,
    uint2 pixelPosition,
    float viewDepth,
    float roughness,
    float3 diffuse,
    float3 specular,
    float diffuseHitT,
    float specularHitT,
    bool isFirstPassDiffuse,
    bool isFirstPassSpecular,
    bool isLastPass)
{
    uint2 lightingTexturePos = (g_Const.denoiserMode != DENOISER_MODE_OFF)
        ? reservoirPosition
        : pixelPosition;

    if (!isFirstPassDiffuse)
    {
        float4 priorDiffuse = u_DiffuseLighting[lightingTexturePos];
        if (CalcLuminance(diffuse) > CalcLuminance(priorDiffuse.rgb) || diffuseHitT == 0)
            diffuseHitT = priorDiffuse.w;
        diffuse += priorDiffuse.rgb;
    }

    if (!isFirstPassSpecular)
    {
        float4 priorSpecular = u_SpecularLighting[lightingTexturePos];
        if (CalcLuminance(specular) > CalcLuminance(priorSpecular.rgb) || specularHitT == 0)
            specularHitT = priorSpecular.w;
        specular += priorSpecular.rgb;
    }

    if (g_Const.denoiserMode == DENOISER_MODE_OFF && g_Const.runtimeParams.activeCheckerboardField != 0 && isLastPass)
    {
        int2 otherFieldPixelPosition = pixelPosition;
        otherFieldPixelPosition.x += (g_Const.runtimeParams.activeCheckerboardField == 1) == ((pixelPosition.y & 1) != 0)
            ? 1 : -1;

        if (g_Const.denoiserMode == DENOISER_MODE_RELAX || g_Const.enableAccumulation)
        {
            diffuse *= 2;
            specular *= 2;

            u_DiffuseLighting[otherFieldPixelPosition] = 0;
            u_SpecularLighting[otherFieldPixelPosition] = 0;
        }
        else // g_Const.denoiserMode == DENOISER_MODE_OFF
        {
            u_DiffuseLighting[otherFieldPixelPosition] = float4(diffuse, 0);
            u_SpecularLighting[otherFieldPixelPosition] = float4(specular, 0);
        }
    }

#if WITH_NRD
    if(g_Const.denoiserMode != DENOISER_MODE_OFF && isLastPass)
    {
        const bool useReLAX = (g_Const.denoiserMode == DENOISER_MODE_RELAX);
        const bool sanitize = true;

        if (useReLAX)
        {
            u_DiffuseLighting[lightingTexturePos] = RELAX_FrontEnd_PackRadianceAndHitDist(diffuse, diffuseHitT, sanitize);
            u_SpecularLighting[lightingTexturePos] = RELAX_FrontEnd_PackRadianceAndHitDist(specular, specularHitT, sanitize);
        }
        else
        {
            float diffNormDist = REBLUR_FrontEnd_GetNormHitDist(diffuseHitT, viewDepth, g_Const.reblurHitDistParams, 1.0);
            u_DiffuseLighting[lightingTexturePos] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(diffuse, diffNormDist, sanitize);

            float specNormDist = REBLUR_FrontEnd_GetNormHitDist(specularHitT, viewDepth, g_Const.reblurHitDistParams, roughness);
            u_SpecularLighting[lightingTexturePos] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(specular, specNormDist, sanitize);
        }
    }
    else
#endif
    {
        u_DiffuseLighting[lightingTexturePos] = float4(diffuse, diffuseHitT);
        u_SpecularLighting[lightingTexturePos] = float4(specular, specularHitT);
    }
}

#endif // SHADING_HELPERS_HLSLI
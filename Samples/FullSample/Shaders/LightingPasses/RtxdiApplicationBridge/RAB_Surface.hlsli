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

#ifndef RTXDI_RAB_SURFACE_HLSLI
#define RTXDI_RAB_SURFACE_HLSLI

#include "../../GBufferHelpers.hlsli"
#include "../../SceneGeometry.hlsli"
#include "SharedShaderInclude/ShaderParameters.h"

#include "Rtxdi/Utils/BrdfRaySample.hlsli"
#include "Rtxdi/Utils/Color.hlsli"
#include "Rtxdi/Utils/RandomSamplerState.hlsli"
#include "RAB_Material.hlsli"

struct RAB_Surface
{
    float3 worldPos;
    float3 viewDir;
    float3 normal;
    float3 geoNormal;
    float viewDepth;
    float diffuseProbability;
    RAB_Material material;
};

RAB_Surface RAB_EmptySurface()
{
    RAB_Surface surface = (RAB_Surface)0;
    surface.viewDepth = BACKGROUND_DEPTH;
    return surface;
}

// Checks if the given surface is valid, see RAB_GetGBufferSurface.
bool RAB_IsSurfaceValid(RAB_Surface surface)
{
    return surface.viewDepth != BACKGROUND_DEPTH;
}

// Returns the world position of the given surface
float3 RAB_GetSurfaceWorldPos(RAB_Surface surface)
{
    return surface.worldPos;
}

void RAB_SetSurfaceWorldPos(inout RAB_Surface surface, float3 worldPos)
{
    surface.worldPos = worldPos;
}

RAB_Material RAB_GetMaterial(RAB_Surface surface)
{
    return surface.material;
}

// World space shading normal
float3 RAB_GetSurfaceNormal(RAB_Surface surface)
{
    return surface.normal;
}

// World space normal of the geometric primitive (i.e. without normal map)
float3 RAB_GetSurfaceGeoNormal(RAB_Surface surface)
{
	return surface.geoNormal;
}

// Vector from position to camera eye or previous bounce
float3 RAB_GetSurfaceViewDir(RAB_Surface surface)
{
	return surface.viewDir;
}

// Returns the linear depth of the given surface.
// It doesn't have to be linear depth in a strict sense (i.e. viewPos.z),
// and can be distance to the camera or primary path length instead.
// Just make sure that the motion vectors' .z component follows the same logic.
float RAB_GetSurfaceLinearDepth(RAB_Surface surface)
{
    return surface.viewDepth;
}

float RAB_GetSurfaceRoughness(RAB_Surface surface)
{
	return surface.material.roughness;
}

void RAB_SetSurfaceNormal(inout RAB_Surface surface, float3 normal)
{
	surface.normal = normal;
}

float GetSurfaceDiffuseProbability(RAB_Surface surface)
{
    float diffuseWeight = CalcLuminance(surface.material.diffuseAlbedo);
    float specularWeight = CalcLuminance(Schlick_Fresnel(surface.material.specularF0, dot(surface.viewDir, surface.normal)));
    float sumWeights = diffuseWeight + specularWeight;
    return sumWeights < 1e-7f ? 1.f : (diffuseWeight / sumWeights);
}

RAB_Surface GetGBufferSurface(
    int2 pixelPosition, 
    bool prevFrame,
    PlanarViewConstants view, 
    Texture2D<float> depthTexture, 
    Texture2D<uint> normalsTexture, 
    Texture2D<uint> geoNormalsTexture)
{
    RAB_Surface surface = RAB_EmptySurface();

    if (any(pixelPosition >= view.viewportSize))
        return surface;

    surface.viewDepth = depthTexture[pixelPosition];

    if(surface.viewDepth == BACKGROUND_DEPTH)
        return surface;

    surface.material = RAB_GetGBufferMaterial(pixelPosition, prevFrame);

    surface.geoNormal = octToNdirUnorm32(geoNormalsTexture[pixelPosition]);
    surface.normal = octToNdirUnorm32(normalsTexture[pixelPosition]);
    surface.worldPos = ViewDepthToWorldPos(view, pixelPosition, surface.viewDepth);
    surface.viewDir = normalize(view.cameraDirectionOrPosition.xyz - surface.worldPos);
    surface.diffuseProbability = GetSurfaceDiffuseProbability(surface);

    return surface;
}

// Reads the G-buffer, either the current one or the previous one, and returns a surface.
// If the provided pixel position is outside of the viewport bounds, the surface
// should indicate that it's invalid when RAB_IsSurfaceValid is called on it.
RAB_Surface RAB_GetGBufferSurface(int2 pixelPosition, bool prevFrame)
{
    if(prevFrame)
    {
        return GetGBufferSurface(
            pixelPosition,
            prevFrame,
            g_Const.prevView,
            t_PrevGBufferDepth, 
            t_PrevGBufferNormals, 
            t_PrevGBufferGeoNormals);
    }
    else
    {
        return GetGBufferSurface(
            pixelPosition, 
            prevFrame,
            g_Const.view, 
            t_GBufferDepth, 
            t_GBufferNormals, 
            t_GBufferGeoNormals);
    }
}

float3 WorldToTangent(RAB_Surface surface, float3 w)
{
    // reconstruct tangent frame based off worldspace normal
    // this is ok for isotropic BRDFs
    // for anisotropic BRDFs, we need a user defined tangent
    float3 tangent;
    float3 bitangent;
    ConstructONB(surface.normal, tangent, bitangent);

    return float3(dot(bitangent, w), dot(tangent, w), dot(surface.normal, w));
}

float3 TangentToWorld(RAB_Surface surface, float3 h)
{
    // reconstruct tangent frame based off worldspace normal
    // this is ok for isotropic BRDFs
    // for anisotropic BRDFs, we need a user defined tangent
    float3 tangent;
    float3 bitangent;
    ConstructONB(surface.normal, tangent, bitangent);

    return bitangent * h.x + tangent * h.y + surface.normal * h.z;
}


float3 RAB_SurfaceEvaluateDeltaReflectionThroughput(RAB_Surface surface, float3 L)
{
    float3 N = surface.normal;
    float NoL = saturate(dot(L, N));
    float3 F0 = surface.material.specularF0;
    float3 F = Schlick_Fresnel(F0, NoL);
    return F / (1.f - surface.diffuseProbability);
}

/*
 * The following interface functions are split between BRDF and BSDF versions
 *
 * The BRDF versions
 *   - DO NOT handle delta bounces (mirrors & refraction)
 *   - Are used by ReSTIR DI
 *
 * The BSDF versions
 *   - DO handle delta bounces (mirrors & refraction)
 *   - Are used by ReSTIR PT
 *
 * This split allows ReSTIR DI, which does not support delta bounces,
 *   to be integrated and maintained separately from ReSTIR PT, which
 *   does support delta bounces.
 *
 */

/*
 *  BRDF surface functions used by ReSTIR DI
 */

/*
 * Evaluate the BRDF for the surface given the outgoing direction
 * Used by ReSTIR DI
 */
float3 RAB_SurfaceEvaluateBrdfTimesNoL(RAB_Surface surface, float3 L)
{
	float3 N = surface.normal;
    float3 V = surface.viewDir;

    if (dot(L, surface.geoNormal) <= 0)
        return 0;

    float d = Lambert(N, -L);
    float3 s = float3(0.0f, 0.0f, 0.0f);
    if (surface.material.roughness >= kMinRoughness)
        s = GGX_times_NdotL(V, L, N, max(surface.material.roughness, kMinRoughness), surface.material.specularF0);

	return d * surface.material.diffuseAlbedo + s;
}

/*
 * Importance sample the BRDF for the surface
 * Used by ReSTIR DI
 */
bool RAB_SurfaceImportanceSampleBrdf(RAB_Surface surface, inout RTXDI_RandomSamplerState rng, out float3 dir)
{
    float3 rand;
    rand.x = RTXDI_GetNextRandom(rng);
    rand.y = RTXDI_GetNextRandom(rng);
    rand.z = RTXDI_GetNextRandom(rng);
    if (rand.x < surface.diffuseProbability)
    {
        float pdf;
        float3 h = SampleCosHemisphere(rand.yz, pdf);
        dir = TangentToWorld(surface, h);
    }
    else
    {
        // Glossy reflection
        float3 Ve = normalize(WorldToTangent(surface, surface.viewDir));
        float3 h = ImportanceSampleGGX_VNDF(rand.yz, max(surface.material.roughness, kMinRoughness), Ve, 1.0);
        h = normalize(h);
        dir = reflect(-surface.viewDir, TangentToWorld(surface, h));
    }

    return dot(surface.normal, dir) > 0.f;
}

/*
 * Evaluate the PDF of the BRDF for the surface given the outgoing direction
 * Used by ReSTIR DI
 */
float RAB_SurfaceEvaluateBrdfPdf(RAB_Surface surface, float3 dir)
{
    float cosTheta = saturate(dot(surface.normal, dir));
    float diffusePdf = cosTheta / M_PI;
    float specularPdf = ImportanceSampleGGX_VNDF_PDF(max(surface.material.roughness, kMinRoughness), surface.normal, surface.viewDir, dir);
    float pdf = cosTheta > 0.f ? lerp(specularPdf, diffusePdf, surface.diffuseProbability) : 0.f;
    return pdf;
}

/*
 * Multiply incoming radiance by the BRDF and cosine term
 * Used by ReSTIR DI
 */
float3 RAB_GetReflectedBrdfRadianceForSurface(float3 incomingRadianceLocation, float3 incomingRadiance, RAB_Surface surface)
{
    float3 L = normalize(incomingRadianceLocation - surface.worldPos);
	float3 brdfTimesNoL = RAB_SurfaceEvaluateBrdfTimesNoL(surface, L);
	return incomingRadiance * brdfTimesNoL;
}

/*
 * Luminance of the reflected radiance for the given surface
 * Used by ReSTIR DI
 */
float RAB_GetReflectedBrdfLuminanceForSurface(float3 incomingRadianceLocation, float3 incomingRadiance, RAB_Surface surface)
{
    return RTXDI_Luminance(RAB_GetReflectedBrdfRadianceForSurface(incomingRadianceLocation, incomingRadiance, surface));
}

/*
 *  BSDF surface functions used by ReSTIR PT
 */

/*
 * Evaluate the BSDF for the surface given the outgoing direction
 * isDelta assumes L is the mirror reflection of -surface.viewDir about its normal.
 * Used by ReSTIR PT
 */
float3 RAB_SurfaceEvaluateBsdfTimesNoL(RAB_Surface surface, float3 L, bool isDelta)
{
	float3 N = surface.normal;
    float3 V = surface.viewDir;

    if (dot(L, surface.geoNormal) <= 0)
        return 0;

    float d = 0;
    float3 s = float3(0.0f, 0.0f, 0.0f);
    if(isDelta)
    {
        float3 N = surface.normal;
        float NoL = saturate(dot(L, N));
        float3 F0 = surface.material.specularF0;
        float3 F = Schlick_Fresnel(F0, NoL);
        s = F / (1.f - surface.diffuseProbability);
    }
    else
    {
        d = Lambert(N, -L);
        if (surface.material.roughness >= kMinRoughness)
        s = GGX_times_NdotL(V, L, N, surface.material.roughness, surface.material.specularF0);
    }

	return d * surface.material.diffuseAlbedo + s;
}

/*
 * Importance sample the BSDF for the surface
 * Used by ReSTIR PT
 */
bool RAB_SurfaceImportanceSampleBsdf(RAB_Surface surface, inout RTXDI_RandomSamplerState rng, out float3 dir, out RTXDI_BrdfRaySampleProperties brsp)
{
    float3 rand;
    rand.x = RTXDI_GetNextRandom(rng);
    rand.y = RTXDI_GetNextRandom(rng);
    rand.z = RTXDI_GetNextRandom(rng);

    brsp.SetReflection();

    if (rand.x < surface.diffuseProbability)
    {
        float pdf;
        float3 h = SampleCosHemisphere(rand.yz, pdf);
        dir = TangentToWorld(surface, h);
        brsp.SetDiffuse();
        brsp.SetContinuous();
    }
    else
    {
        brsp.SetSpecular();
        // Perfect mirror reflection
        if(surface.material.roughness < kMinRoughness)
        {
            dir = reflect(-surface.viewDir, surface.normal);
            brsp.SetDelta();
        }
        // Glossy reflection
        else
        {
            float3 Ve = normalize(WorldToTangent(surface, surface.viewDir));
            float3 h = ImportanceSampleGGX_VNDF(rand.yz, max(surface.material.roughness, kMinRoughness), Ve, 1.0);
            h = normalize(h);
            dir = reflect(-surface.viewDir, TangentToWorld(surface, h));
            brsp.SetContinuous();
        }
    }

    return dot(surface.normal, dir) > 0.f;
}

/*
 * Evaluate the PDF of the BSDF for the surface given the outgoing direction
 * Used by ReSTIR PT
 */
float RAB_SurfaceEvaluateBsdfPdf(RAB_Surface surface, float3 dir, RTXDI_BrdfRaySampleProperties brsp)
{
    float cosTheta = saturate(dot(surface.normal, dir));
    float diffusePdf = cosTheta / M_PI;
    float specularPdf = 0.0;
    if(surface.material.roughness >= kMinRoughness)
    {
        specularPdf = ImportanceSampleGGX_VNDF_PDF(max(surface.material.roughness, kMinRoughness), surface.normal, surface.viewDir, dir);
    }
    else if(brsp.IsDelta())
    {
        specularPdf = 1.0;
    }
    float pdf = cosTheta > 0.f ? lerp(specularPdf, diffusePdf, surface.diffuseProbability) : 0.f;
    return pdf;
}

/*
 * Multiply incoming radiance by the BSDF and cosine term
 * Used by ReSTIR PT
 */
float3 RAB_GetReflectedBsdfRadianceForSurface(float3 incomingRadianceLocation, float3 incomingRadiance, RAB_Surface surface)
{
    float3 L = normalize(incomingRadianceLocation - surface.worldPos);
	float3 brdfTimesNoL = RAB_SurfaceEvaluateBsdfTimesNoL(surface, L, false);
	return incomingRadiance * brdfTimesNoL;
}

#ifdef SHADER_DEBUG_PRINT_FULL_HLSLI
void PrintSurface_(RAB_Surface surface)
{
    DebugPrint_("RAB_Surface:");
    DebugPrint_("- worldPos: {0}", surface.worldPos);
    DebugPrint_("- viewDir: {0}", surface.viewDir);
    DebugPrint_("- normal: {0}", surface.normal);
    DebugPrint_("- geoNormal: {0}", surface.geoNormal);
    DebugPrint_("- viewDepth: {0}", surface.viewDepth);
    DebugPrint_("- diffuseProbability: {0}", surface.diffuseProbability);
    DebugPrint_("- material.diffuseAlbedo: {0}", surface.material.diffuseAlbedo);
    DebugPrint_("- material.specularF0: {0}", surface.material.specularF0);
    DebugPrint_("- material.roughness: {0}", surface.material.roughness);
    DebugPrint_("- material.emissiveColor: {0}", surface.material.emissiveColor);
}

#endif // SHADER_DEBUG_PRINT_FULL_HLSLI

#endif // RTXDI_RAB_SURFACE_HLSLI
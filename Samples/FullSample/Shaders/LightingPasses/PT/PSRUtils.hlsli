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

#ifndef PRIMARY_SURFACE_REPLACEMENT_UTILS_HLSLI
#define PRIMARY_SURFACE_REPLACEMENT_UTILS_HLSLI

#include "Rtxdi/PT/Reservoir.hlsli"
#include "Rtxdi/Utils/BrdfRaySample.hlsli"
#include "../RtxdiApplicationBridge/RAB_LightSample.hlsli"
#include "../RtxdiApplicationBridge/RAB_Surface.hlsli"

//
// PSR = Primary Surface Replacement
// PSR updates the guide buffers for the denoiser (NRD)
//     when the first bounces in the path tracer are mirrors
// PSR lets the denoiser denoiser the reflected geometry,
//     which is noisy, rather than the mirror, which is not
//

struct PSRData
{
    //
    // Output variables
    //

    // Additional distance traveled in world space by ray
    // Add this to primary ray viewZ for NRD guide buffer
    float additionalViewZ;

    // Depth in camera space of first non-mirror surface
    float depth;

    // Normal at first non-mirror surface
    float3 normal;

    // Motion vector at first non-mirror surface
    float4 motionVectors;

    // Roughness at first non-mirror surface
    float roughness;

    // Distance from first non-mirror surface to light source/next vertex
    // Used for specular hitT calculations
    float hitT;
    float mainPathHitT;

    // direction from first non-mirror surface to light source/next vertex
    // Used for demodulation
    uint packedL;
    uint mainPathPackedL;

    // Diffuse albedo at first non-mirror surface
    uint packedDiffuseAlbedo;
    uint packedSpecularF0;

    //
    // Input variables
    //

    float3 primarySurfaceWorldPos;

    float3 primarySurfaceViewDir;

    bool updateWithResampling;

    //
    // Internal state
    //

    // Internal matrix to keep track of reflections
    // Applied to normal vector to ensure proper orientation
    float3x3 mirrorMatrix;

    // Internal variables indicating whether psr should be updated
    bool enabled;
    bool active;
    bool shouldRecord;
    bool hitTIsDiffuse;

    bool hasRecorded()
    {
        return depth != 0.f;
    }
};

PSRData GetEmptyPSRData()
{
    PSRData psr = (PSRData)0;
    psr.mirrorMatrix[0] = float3(1.0, 0.0, 0.0);
    psr.mirrorMatrix[1] = float3(0.0, 1.0, 0.0);
    psr.mirrorMatrix[2] = float3(0.0, 0.0, 1.0);
    return psr;
}

void EnablePSRForPathType(
    inout PSRData psr,
    RTXDI_PTPathTraceInvocationType pathType,
    bool updateWithResampling)
{
    // Initial sampling can run multiple times
    // However, using the PSR data from the selected sample requires
    // either storing the data every time a sample is selected, which
    // incurs memory traffic, or storing a second copy of the PSR
    // data associated with the selected sample, which incurs register
    // overhead. In either case, the cost is high relative to the gains,
    // which are mostly invalidated by subsequent resampling passes anyway.
    // Thus, we just calculate PSR for the first sample.
    // Besides, you shouldn't be taking more than 1 initial sample to
    // begin with.
    if (pathType == RTXDI_PTPathTraceInvocationType_Initial)
    {
        psr.enabled = psr.additionalViewZ == 0;
    }
    // Only calculate PSR for the shifted path, not the inverse,
    // which is only used for ensuring correctness.
    else if(updateWithResampling &&
          (pathType == RTXDI_PTPathTraceInvocationType_Temporal ||
           pathType == RTXDI_PTPathTraceInvocationType_Spatial))
       psr.enabled = true;
    else
        psr.enabled = false;
}

void SetPSRPrimarySurfaceDepth(inout PSRData psr, float viewDepth)
{
    if(psr.enabled)
        psr.motionVectors.w = abs(viewDepth);
}

//
// Encoding depends on the texture storage type of the NormalRoughness texture
//
float4 EncodeNormalRoughnessForNRD(float3 normal, float roughness)
{
    return float4(normal * 0.5 + 0.5, roughness);
}

float3x3 GetMirrorMatrix(float3 n)
{
    float3x3 m;
    m[ 0 ] = float3( 1.0f - 2.0f * n.x * n.x, 0.0f - 2.0f * n.y * n.x, 0.0f - 2.0f * n.z * n.x );
    m[ 1 ] = float3( 0.0f - 2.0f * n.x * n.y, 1.0f - 2.0f * n.y * n.y, 0.0f - 2.0f * n.z * n.y );
    m[ 2 ] = float3( 0.0f - 2.0f * n.x * n.z, 0.0f - 2.0f * n.y * n.z, 1.0f - 2.0f * n.z * n.z );

    return m;
}

void UpdatePSRFromRaySample(
    inout PSRData psr,
    const RTXDI_BrdfRaySample raySample,
    const uint bounceDepth)
{
    if(!psr.enabled)
        return;

    psr.shouldRecord = false;

    if (!raySample.properties.IsDelta() && psr.mainPathHitT == 0.f)
    {
        psr.mainPathHitT = -1.f;
    }

    if(bounceDepth == 2 && raySample.properties.IsDelta())
    {
        psr.active = true;
        psr.shouldRecord = true;
    }
    else if(bounceDepth > 2 && raySample.properties.IsDelta() && psr.active)
    {
        psr.shouldRecord = true;
    }
    else
    {
        if (psr.active && psr.hasRecorded())
        {
            float3 L = raySample.outDirection;
            psr.mainPathPackedL = ndirToOctUnorm32(mul(L, psr.mirrorMatrix));
        }

        psr.active = false;
    }
}

float3 GetPSRMotionVector(float4 virtualPos, float4 virtualPosPrev)
{
    float4 clipPos = mul(virtualPos, g_Const.view.matWorldToClip);
    clipPos.xyz /= clipPos.w;
    float4 prevClipPos = mul(virtualPosPrev, g_Const.prevView.matWorldToClip);
    prevClipPos.xyz /= prevClipPos.w;

    float3 motion;
    motion.xy = (prevClipPos.xy - clipPos.xy) * g_Const.view.clipToWindowScale;
    motion.xy += (g_Const.view.pixelOffset - g_Const.prevView.pixelOffset);
    motion.z = prevClipPos.w - clipPos.w;

    return motion;
}

void UpdatePSRFromHit(
    inout PSRData psr,
    const float committedRayT,
    const GeometrySample gs,
    const MaterialSample ms,
    const RAB_Surface surface,
    const RAB_Surface prevSurface,
    const float4x4 matWorldToView)
{
    if(!psr.enabled)
        return;

    if (psr.mainPathHitT < 0.f)
    {
        psr.mainPathHitT = committedRayT;
    }

    if(psr.shouldRecord)
    {
        psr.additionalViewZ += committedRayT;

        float4 prevWorldPos = float4(mul(gs.instance.prevTransform, float4(gs.prevObjectSpacePosition, 1.0)), 1.0);

        float4 virtualPos = float4(psr.primarySurfaceWorldPos - (psr.additionalViewZ * psr.primarySurfaceViewDir), 1.0f);

        psr.depth = mul(virtualPos, matWorldToView).z;

        psr.mirrorMatrix = mul(psr.mirrorMatrix, GetMirrorMatrix(prevSurface.normal));

        float4 virtualPosPrev = float4(virtualPos.xyz + mul(prevWorldPos.xyz - surface.worldPos, psr.mirrorMatrix), 1.0f);
        psr.motionVectors.xyz = GetPSRMotionVector(virtualPos, virtualPosPrev);

        psr.normal = mul(RAB_GetSurfaceNormal(surface), psr.mirrorMatrix);
        psr.roughness = RAB_GetRoughness(RAB_GetMaterial(surface));

        psr.packedDiffuseAlbedo = Pack_R11G11B10_UFLOAT(surface.material.diffuseAlbedo);
        psr.packedSpecularF0 = Pack_R11G11B10_UFLOAT(surface.material.specularF0);
    }
}

void UpdatePSRFromNee(
    inout PSRData psr,
    const RAB_LightSample lightSample,
    const RAB_Surface surface)
{
    if(!psr.enabled)
        return;

    float3 lightVec = lightSample.position - surface.worldPos;
    float lightDistance = length(lightVec);
    psr.hitT = psr.mainPathHitT == 0.f ? lightDistance : psr.mainPathHitT;
    psr.packedL = psr.mainPathHitT == 0.f ? ndirToOctUnorm32(mul(lightVec/lightDistance, psr.mirrorMatrix)) : psr.mainPathPackedL;

}

void FinalizePSRForInitialSampling(
    inout PSRData psr,
    const RTXDI_PTReservoir reservoir)
{
    if (psr.enabled)
    {
        bool isNEESample = RTXDI_ConnectsToNeeLight(reservoir);
        if (!isNEESample)
        {
            psr.hitT = psr.mainPathHitT;
            psr.packedL = psr.mainPathPackedL;
        }
    }
}

void FinalizePSRForHybridShift(inout PSRData psr, RTXDI_PTPathTraceInvocationType pathType)
{
    if(!psr.enabled)
        return;

    if ((pathType == RTXDI_PTPathTraceInvocationType_Temporal) ||
        (pathType == RTXDI_PTPathTraceInvocationType_Spatial))
    {
        psr.hitT = psr.mainPathHitT;
        psr.packedL = psr.mainPathPackedL;
    }
}

void UpdatePSRBuffers(PSRData psr, const int2 pixelPos, const uint pathType)
{
    if (g_Const.enableDenoiserPSR)
    {
        // always want to update light distance with resampling because it affects denoiser blurring radius
        u_PSRHitT[pixelPos] = max(0.f, psr.hitT);

        if (psr.additionalViewZ > 0.f)
        {
            // want to update these with resampling (if enabled) because it affects demodulation
            u_PSRDiffuseAlbedo[pixelPos] = psr.packedDiffuseAlbedo;
            u_PSRSpecularF0[pixelPos] = psr.packedSpecularF0;

            // psr.hitT == 0.f in resampling means that a delta path hits an emissive and we don't have a L value, causing denoiser problem
            // falling back to L in initial resampling.
            if (pathType == RTXDI_PTPathTraceInvocationType_Initial || psr.hitT > 0.f)
                u_PSRLightDir[pixelPos] = psr.packedL;

            u_PSRNormalRoughness[pixelPos] = EncodeNormalRoughnessForNRD(psr.normal, psr.roughness);

             // these have less impact on results, so only populate them in initial resampling and don't update with resampling
            if (pathType == RTXDI_PTPathTraceInvocationType_Initial)
            {
                u_PSRDepth[pixelPos] = psr.depth;
                u_PSRMotionVectors[pixelPos] = psr.motionVectors;
            }
        }
        else // make sure that we write zero values for non-PSR surfaces (we use these values to determine the demodulation operation)
        {
            u_PSRDiffuseAlbedo[pixelPos] = 0;
            u_PSRSpecularF0[pixelPos] = 0;
        }
    }
}

void PrintPSR_(const PSRData psr)
{
    DebugPrint_("PSR:");
    DebugPrint_("- additionalViewZ: {0}", psr.additionalViewZ);
    DebugPrint_("- depth: {0}", psr.depth);
    DebugPrint_("- normal: {0}", psr.normal);
    DebugPrint_("- motionVectors: {0}", psr.motionVectors);
    DebugPrint_("- roughness: {0}", psr.roughness);
    DebugPrint_("- hitT: {0}", psr.hitT);
    DebugPrint_("- mainPathHitT: {0}", psr.mainPathHitT);
    DebugPrint_("- packedL: {0}", psr.packedL);
    DebugPrint_("- mainPathPackedL: {0}", psr.mainPathPackedL);
    DebugPrint_("- packedDiffuseAlbedo: {0}", psr.packedDiffuseAlbedo);
    DebugPrint_("- packedSpecularF0: {0}", psr.packedSpecularF0);
    DebugPrint_("- enabled: {0}", psr.enabled);
}

#endif // PRIMARY_SURFACE_REPLACEMENT_UTILS_HLSLI

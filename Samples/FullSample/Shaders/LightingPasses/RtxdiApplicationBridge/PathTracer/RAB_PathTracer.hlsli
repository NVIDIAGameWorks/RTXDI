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

#ifndef RAB_PATH_TRACING_HLSLI
#define RAB_PATH_TRACING_HLSLI

#include "Rtxdi/DI/Reservoir.hlsli"
#include "Rtxdi/DI/InitialSampling.hlsli"
#include "Rtxdi/DI/SpatialResampling.hlsli"
#include "Rtxdi/PT/PathTracerContext.hlsli"
#include "Rtxdi/PT/PathTracerRandomContext.hlsli"
#include "Rtxdi/Utils/SampledLightData.hlsli"

#include "../../../GBufferHelpers.hlsli"

template<typename PTContextType>
void InitializeDebugPathViz(const RTXDI_PathTracerContext<PTContextType> ctx, const RAB_PathTracerUserData ptud)
{
    Debug_BeginPath(ptud.pathType);

    Debug_RecordPTCameraPosition(g_Const.view.cameraDirectionOrPosition.xyz);
    Debug_SetPTVertexIndex(1);
    Debug_RecordPTIntersectionPosition(RAB_GetSurfaceWorldPos(ctx.GetIntersectionSurface()));
    Debug_RecordPTIntersectionNormal(RAB_GetSurfaceNormal(ctx.GetIntersectionSurface()));
}

bool IsEmissiveSamplingEnabled()
{
    return g_Const.pt.lightSamplingMode == PT_INITIAL_SAMPLING_LIGHT_SAMPLING_MODE_EMISSIVE_ONLY ||
           g_Const.pt.lightSamplingMode == PT_INITIAL_SAMPLING_LIGHT_SAMPLING_MODE_MIS;
}

bool IsEmissiveOnlySamplingEnabled()
{
    return g_Const.pt.lightSamplingMode == PT_INITIAL_SAMPLING_LIGHT_SAMPLING_MODE_EMISSIVE_ONLY;
}

bool IsNeeSamplingEnabled()
{
    return g_Const.pt.lightSamplingMode == PT_INITIAL_SAMPLING_LIGHT_SAMPLING_MODE_NEE_ONLY ||
           g_Const.pt.lightSamplingMode == PT_INITIAL_SAMPLING_LIGHT_SAMPLING_MODE_MIS;
}

bool IsNeeOnlySamplingEnabled()
{
    return g_Const.pt.lightSamplingMode == PT_INITIAL_SAMPLING_LIGHT_SAMPLING_MODE_NEE_ONLY;
}

template<typename PTContextType>
bool ShouldRunPrimaryBrdfSampling(const RTXDI_PathTracerContext<PTContextType> ctx)
{
    return ctx.GetBounceDepth() != 1;
}

bool ShouldRunRussianRoulette(PSRData psr)
{
    return g_Const.pt.enableRussianRoulette && !psr.active;
}

RTXDI_BrdfRaySample ImportanceSampleBrdf(RAB_Surface surface, inout RTXDI_RandomSamplerState rng)
{
    float3 outDir = float3(0.0f, 0.0f, 0.0f);
    RTXDI_BrdfRaySample brs = RTXDI_EmptyBrdfRaySample();
    RTXDI_BrdfRaySampleProperties brsp = RTXDI_DefaultBrdfRaySampleProperties();
    bool validOutDir = RAB_SurfaceImportanceSampleBsdf(surface, rng, outDir, brsp);

    if(validOutDir)
    {
        brs.outDirection = outDir;
        brs.properties = brsp;
        brs.outPdf = RAB_SurfaceEvaluateBsdfPdf(surface, outDir, brsp);
        brs.brdfTimesNoL = RAB_SurfaceEvaluateBsdfTimesNoL(surface, outDir, brsp.IsDelta());
    }
    return brs;
}

template<typename PTContextType>
void UpdateBounceDepthFromRaySample(inout RTXDI_PathTracerContext<PTContextType> ctx,
                                    inout uint extraMirrorBounceBudget,
                                    const RAB_PathTracerUserData ptud)
{
    RTXDI_BrdfRaySample raySample = ctx.GetBrdfRaySample();
    if(raySample.properties.IsDelta() && extraMirrorBounceBudget > 0 && ptud.pathType == RTXDI_PTPathTraceInvocationType_Initial)
    {
        ctx.SetMaxPathBounce((uint16_t)ctx.GetMaxPathBounce() + 1);
        ctx.SetMaxRcVertexLengthIfUnset(ctx.GetRcVertexLength() + 1);
        extraMirrorBounceBudget--;
    }
}

template<typename PTContextType>
bool ThroughputIsBelowMinimumThreshold(const RTXDI_PathTracerContext<PTContextType> ctx, const RAB_PathTracerUserData ptud)
{
    // Only apply to initial sampling pass.
    if(ptud.pathType != RTXDI_PTPathTraceInvocationType_Initial)
        return false;

    // Calculate square to avoid sqrt
    return dot(ctx.GetPathThroughput(), ctx.GetPathThroughput()) < g_Const.pt.minimumPathThroughput*g_Const.pt.minimumPathThroughput;
}

template<typename PTContextType>
float CalculateRussianRouletteContinuationProbability(const RTXDI_PathTracerContext<PTContextType> ctx)
{
#if 1
    return g_Const.pt.russianRouletteContinueChance;
#else
    // TODO: Fix Vulkan implementation
#ifdef SPIRV
    return g_Const.pt.russianRouletteContinueChance;
#else // #ifdef DONUT_WITH_DX12
    // TODO: The following logic should be used instead, but does not compile in SPIR-V.
    const float ThreadContinuationProb = min(1.f, RTXDI_Luminance(ctx.GetPathThroughput()));

    // Be more aggressive in terminating paths if these are the only few threads left in a warp.
    const uint ActivePathCount = WaveActiveCountBits(true);
    const uint TotalLaneCount = WaveGetLaneCount();
    const float ActivePathRatio = (float)ActivePathCount / (float)TotalLaneCount;
    const float ActivePathRatioThreshold = 0.3f;
    const float GroupContinuationProb = ThreadContinuationProb * saturate(ActivePathRatio / ActivePathRatioThreshold);

    return GroupContinuationProb;
#endif
#endif
}

// Computes new ray origin based on hit position to avoid self-intersections.
// The function assumes that the hit position has been computed by barycentric
// interpolation, and not from the ray t which is less accurate.
//
// The method is described in Ray Tracing Gems, Chapter 6, "A Fast and Robust
// Method for Avoiding Self-Intersection" by Carsten W�chter and Nikolaus Binder.
// https://developer.nvidia.com/ray-tracing-gems-ii
//
// Note `rayOrigin` and `geoNormal` can be in any domain as long as they are in
// the same domain.
float3 AdjustRayOrigin(float3 rayOrigin, float3 geoNormal)
{
    const float origin = 1.f / 16.f;
    const float scale0 = 3.f / 65536.f;
    const float scale1 = 3.f * 256.f;

    // Per-component integer offset to bit representation of fp32 position.
    const int3 offset = int3(geoNormal * scale1);
    const float3 newRayOrigin = asfloat(asint(rayOrigin) + select(rayOrigin < 0.f, -offset, offset));

    // Select per-component between small fixed offset or above variable offset depending on distance to origin.
    const float3 resolvedOffset = geoNormal * scale0;
    return select(abs(rayOrigin) < origin, rayOrigin + resolvedOffset, newRayOrigin);
}

RayDesc setupContinuationRay(float3 worldNormal, float3 worldPosition, float3 outDirection)
{
    const float NoL = dot(outDirection, worldNormal);

    RayDesc continuationRay = (RayDesc)0;
    // TODO: The AdjustRayOrigin method doesn't prevent self intersection, so setting we just
    //		 set the TMin to a very small number instead.
    continuationRay.Origin = AdjustRayOrigin(worldPosition, NoL > 0.0f ? worldNormal : -worldNormal);
    continuationRay.Direction = outDirection;
    continuationRay.TMax = RTXDI_MAX_FLOAT32;
    continuationRay.TMin = 0.00001f;
    return continuationRay;
}

RAB_RayPayload TraceNextBounce(const RayDesc continuationRay)
{
    RAB_RayPayload traceResult = (RAB_RayPayload)0;
    traceResult.instanceID = ~0u;
    traceResult.throughput = 1.0;

    uint instanceMask = INSTANCE_MASK_OPAQUE;

    if (g_Const.sceneConstants.enableAlphaTestedGeometry)
        instanceMask |= INSTANCE_MASK_ALPHA_TESTED;

    if (g_Const.sceneConstants.enableTransparentGeometry)
        instanceMask |= INSTANCE_MASK_TRANSPARENT;

#if USE_RAY_QUERY
    RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> rayQuery;

    rayQuery.TraceRayInline(SceneBVH, RAY_FLAG_NONE, instanceMask, continuationRay);

    while (rayQuery.Proceed())
    {
        if (rayQuery.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            if (ConsiderTransparentMaterial(
                rayQuery.CandidateInstanceID(),
                rayQuery.CandidateGeometryIndex(),
                rayQuery.CandidatePrimitiveIndex(),
                rayQuery.CandidateTriangleBarycentrics(),
                traceResult.throughput))
            {
                rayQuery.CommitNonOpaqueTriangleHit();
            }
        }
    }

    if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        traceResult.instanceID = rayQuery.CommittedInstanceID();
        traceResult.geometryIndex = rayQuery.CommittedGeometryIndex();
        traceResult.primitiveIndex = rayQuery.CommittedPrimitiveIndex();
        traceResult.barycentrics = rayQuery.CommittedTriangleBarycentrics();
        traceResult.committedRayT = rayQuery.CommittedRayT();
        traceResult.frontFace = rayQuery.CommittedTriangleFrontFace();
    }
#else
    TraceRay(SceneBVH, RAY_FLAG_NONE, instanceMask, 0, 0, 0, continuationRay, traceResult);
#endif
	return traceResult;
}

RAB_Surface LoadSurfaceFromRayPayload(RAB_RayPayload rayPayload, RayDesc ray, const RAB_Surface prevSurface, inout PSRData psr)
{
    GeometrySample gs = getGeometryFromHit(
    rayPayload.instanceID,
    rayPayload.geometryIndex,
    rayPayload.primitiveIndex,
    rayPayload.barycentrics,
    GeomAttr_All,
    t_InstanceData, t_GeometryData, t_MaterialConstants);

    MaterialSample ms = sampleGeometryMaterial(gs, 0, 0, 0,
        MatAttr_All, s_MaterialSampler);

    ms.shadingNormal = getBentNormal(gs.flatNormal, ms.shadingNormal, ray.Direction);

    BRDFPathTracing_MaterialOverrideParameters materialOverrideParams = g_Const.brdfPT.materialOverrideParams;

    if (materialOverrideParams.roughnessOverride >= 0)
        ms.roughness = materialOverrideParams.roughnessOverride;

    if (materialOverrideParams.metalnessOverride >= 0)
    {
        ms.metalness = materialOverrideParams.metalnessOverride;
        GetReflectivity(ms.metalness, ms.baseColor, ms.diffuseAlbedo, ms.specularF0);
    }

    RAB_Surface surface = RAB_EmptySurface();
    surface.worldPos = ray.Origin + ray.Direction * rayPayload.committedRayT;
    surface.geoNormal = gs.geometryNormal;
    surface.viewDir = -ray.Direction;
    surface.normal = (dot(ms.shadingNormal, ray.Direction) < 0) ? ms.shadingNormal : -ms.shadingNormal;
    surface.material.diffuseAlbedo = ms.diffuseAlbedo;
    surface.material.specularF0 = ms.specularF0;
    surface.material.roughness = ms.roughness;
    surface.material.emissiveColor = ms.emissiveColor; // originally .radiance += ms.emissiveColor
    surface.diffuseProbability = GetSurfaceDiffuseProbability(surface);
    surface.viewDepth = 1.0; // doesn't matter

    UpdatePSRFromHit(psr, rayPayload.committedRayT, gs, ms, surface, prevSurface, g_Const.view.matWorldToView);

    return surface;
}

template<typename PTContextType>
bool ShouldSampleEmissiveSurfaces(const RTXDI_PathTracerContext<PTContextType> ctx, RAB_Surface prevSurface)
{
    if(!ctx.ShouldSampleEmissiveSurfaces())
        return false;

    // Special case mirror surfaces to always sample emissive on secondary hit
    // Since DI doesn't handle mirrors anyway, this case applies to both NEE and emissive sampling
    if (prevSurface.material.roughness < kMinRoughness && ctx.GetBounceDepth() == 2)
        return true;

    // Otherwise ignore emissive sampling for NEE-only mode
    if(IsEmissiveSamplingEnabled())
    {
        // This global bool should probably always be false to avoid double counting emissives for DI
        // Otherwise sample all emissives after the secondary surface.
        if(g_Const.pt.sampleEmissivesOnSecondaryHit || ctx.GetBounceDepth() > 2)
            return true;
    }
    return false;
}

template<typename PTContextType>
float3 SampleEmissiveLightFromSurface(const RTXDI_PathTracerContext<PTContextType> ctx)
{
    const bool FacingNextSurface = RAB_RayPayloadIsFrontFace(ctx.GetTraceResult());
    return (FacingNextSurface ? RAB_GetEmissiveColor(RAB_GetMaterial(ctx.GetIntersectionSurface())) : float3(0.0f, 0.0f, 0.0f));
}

template<typename PTContextType>
bool ShouldSampleNeeLights(const RTXDI_PathTracerContext<PTContextType> ctx)
{
    return IsNeeSamplingEnabled()
           && ctx.ShouldSampleNee()
           && ctx.GetIntersectionSurface().material.roughness >= kMinRoughness;
}

void SampleDirectLightsForIndirectSurface(inout RTXDI_RandomSamplerState rng,
                                          inout RTXDI_RandomSamplerState tileRng,
                                          RAB_Surface secondarySurface,
                                          inout RAB_LightSample lightSample,
                                          inout RTXDI_DIReservoir diReservoir)
{
    RTXDI_DIInitialSamplingParameters sampleParams = g_Const.pt.nee.initialSamplingParams;

    diReservoir = RTXDI_SampleLightsForSurface(rng, tileRng, secondarySurface,
        sampleParams, g_Const.lightBufferParams,
#if RTXDI_ENABLE_PRESAMPLING
    g_Const.localLightsRISBufferSegmentParams, g_Const.environmentLightRISBufferSegmentParams,
#if RTXDI_REGIR_MODE != RTXDI_REGIR_MODE_DISABLED
    g_Const.regir,
#endif
#endif
    lightSample);

    if (g_Const.pt.enableSecondaryDISpatialResampling)
    {
        // Try to find this secondary surface in the G-buffer. If found, resample the lights
        // from that G-buffer surface into the reservoir using the spatial resampling function.

        float4 secondaryClipPos = mul(float4(secondarySurface.worldPos, 1.0), g_Const.view.matWorldToClip);
        secondaryClipPos.xyz /= secondaryClipPos.w;

        if (all(abs(secondaryClipPos.xy) < 1.0) && secondaryClipPos.w > 0)
        {
            int2 secondaryPixelPos = int2(secondaryClipPos.xy * g_Const.view.clipToWindowScale + g_Const.view.clipToWindowBias);
            RAB_Surface is = secondarySurface;
            is.viewDepth = secondaryClipPos.w;

            RTXDI_DISpatialResamplingParameters sparams = g_Const.pt.nee.spatialResamplingParams;
            uint sourceBufferIndex = g_Const.restirDI.bufferIndices.shadingInputBufferIndex;

            diReservoir = RTXDI_DISpatialResampling(secondaryPixelPos, is, diReservoir,
                rng, g_Const.runtimeParams, g_Const.restirDI.reservoirBufferParams, sourceBufferIndex, sparams, lightSample);
        }
    }
}

void CalculateNEE(const RAB_Surface intersectionSurface,
                  inout RTXDI_PathTracerRandomContext ptRandomContext,
                  inout float3 radianceFromLights,
                  inout RTXDI_SampledLightData sampledLightData,
                  inout float neePdf,
                  inout float scatterPdf,
                  inout RAB_LightSample lightSample)
{
    RTXDI_DIReservoir diReservoir = RTXDI_EmptyDIReservoir();
    SampleDirectLightsForIndirectSurface(ptRandomContext.initialRandomSamplerState, ptRandomContext.initialCoherentRandomSamplerState, intersectionSurface, lightSample, diReservoir);

    Debug_RecordPTNEELightPosition(RAB_LightSamplePosition(lightSample));

    radianceFromLights = float3(0.0f, 0.0f, 0.0f);

    if (diReservoir.weightSum > 0.0f && lightSample.solidAnglePdf > 0)
    {
        float3 L = normalize(lightSample.position - intersectionSurface.worldPos);
        radianceFromLights = RAB_GetReflectedBsdfRadianceForSurface(lightSample.position, lightSample.radiance, intersectionSurface);
        radianceFromLights *= RAB_GetConservativeVisibility(intersectionSurface, lightSample.position);
        radianceFromLights *= RTXDI_GetDIReservoirInvPdf(diReservoir) / lightSample.solidAnglePdf;

        sampledLightData.lightData = diReservoir.lightData;
        sampledLightData.uvData = diReservoir.uvData;
        neePdf = 1.0f / diReservoir.weightSum;
        scatterPdf = RAB_SurfaceEvaluateBrdfPdf(intersectionSurface, normalize(RAB_LightSamplePosition(lightSample) - RAB_GetSurfaceWorldPos(intersectionSurface)));
    }

}

template<typename PTContextType>
void HandleHit(inout RTXDI_PathTracerContext<PTContextType> ctx,
               inout RTXDI_PathTracerRandomContext ptRandContext,
               const RAB_Surface prevSurface,
               inout PSRData psr)
{
    RAB_Surface intersectionSurface = LoadSurfaceFromRayPayload(ctx.GetTraceResult(), ctx.GetContinuationRay(), ctx.GetIntersectionSurface(), psr);

    ctx.RecordPathIntersection(intersectionSurface);

    Debug_RecordPTIntersectionPosition(RAB_GetSurfaceWorldPos(ctx.GetIntersectionSurface()));
    Debug_RecordPTIntersectionNormal(RAB_GetSurfaceNormal(ctx.GetIntersectionSurface()));

    if(ctx.IsPathTerminated())
        return;

    if(ShouldSampleEmissiveSurfaces(ctx, prevSurface))
    {
        float3 emissiveLight = SampleEmissiveLightFromSurface(ctx);
        emissiveLight *= GetMISWeightForEmissiveSurface(ctx.GetTraceResult(), prevSurface, ctx.GetBrdfRaySample());

        ctx.RecordEmissiveLightSample(emissiveLight, prevSurface, ptRandContext.initialRandomSamplerState);
    }

    if(ShouldSampleNeeLights(ctx))
    {
        float3 radianceFromLights = float3(0.0f, 0.0f, 0.0f);
        RTXDI_SampledLightData sampledLightData = RTXDI_SampledLightData_CreateInvalidData();
        RAB_LightSample lightSample = RAB_EmptyLightSample();
        float neePdf = 0.0f;
        float scatterPdf = 0.0f;
        CalculateNEE(ctx.GetIntersectionSurface(), ptRandContext, radianceFromLights, sampledLightData, neePdf, scatterPdf, lightSample);
        radianceFromLights *= GetMISWeightForNEELight(RTXDI_SampledLightData_GetLightIndex(sampledLightData), lightSample, ctx.GetIntersectionSurface().worldPos, scatterPdf);

        bool accepted = false;

        if (neePdf != 0)
            accepted = ctx.RecordNeeLightSample(sampledLightData, radianceFromLights, neePdf, scatterPdf, lightSample, ptRandContext.initialRandomSamplerState);

        if (accepted)
            UpdatePSRFromNee(psr, lightSample, intersectionSurface);
    }
    else
    {
        Debug_RecordPTNEELightPosition(ctx.GetIntersectionSurface().worldPos);
    }
}

template<typename PTContextType>
bool ShouldRecordEnvironmentMapLightSample(const RTXDI_PathTracerContext<PTContextType> ctx)
{
    if(!g_Const.sceneConstants.enableEnvironmentMap)
        return false;
    // Evaluate env map lighting for primary surface.
    // This option should probably never be enabled since the DI lighting solution
    //     should handle env map lighting for primary surface.
    // Should always record environment map light for delta reflection
    if(ctx.GetBounceDepth() == 2)
    {
        // Sample env map on secondary miss if the previous surface was a delta surface
        if(ctx.GetBrdfRaySample().properties.IsDelta())
            return true;
        else
            return g_Const.pt.sampleEnvMapOnSecondaryMiss;
    }

    // Sample env map on miss when it couldn't have been sampled by the NEE
    //     for the previous surface.
    if(g_Const.pt.lightSamplingMode == IsNeeOnlySamplingEnabled())
    {
        return g_Const.pt.nee.initialSamplingParams.numEnvironmentSamples == 0;
    }
    else if(ctx.GetBounceDepth() > 2)
        return true;
    else
        return false;
}

template<typename PTContextType>
bool DetermineOutgoingRayAndThroughput(inout RTXDI_PathTracerContext<PTContextType> ctx, inout RTXDI_PathTracerRandomContext ptRandContext, inout RAB_PathTracerUserData ptud, inout uint extraMirrorBounceBudget)
{
    Debug_SetPTVertexIndex(ctx.GetBounceDepth());
    if (ShouldRunPrimaryBrdfSampling(ctx))
    {
        ctx.SetBrdfRaySample(ImportanceSampleBrdf(ctx.GetIntersectionSurface(), ptRandContext.replayRandomSamplerState));
        UpdatePSRFromRaySample(ptud.psr, ctx.GetBrdfRaySample(), ctx.GetBounceDepth());
        UpdateBounceDepthFromRaySample(ctx, extraMirrorBounceBudget, ptud);

        if(!ctx.ValidContinuationRayBrdfOverPdf())
        {
            return false;
        }

        ctx.MultiplyPathThroughput(ctx.GetContinuationRayBrdfOverPdf());

        if(ThroughputIsBelowMinimumThreshold(ctx, ptud))
            return false;

        if(ShouldRunRussianRoulette(ptud.psr) && ctx.ShouldRunRussianRoulette())
        {
            float continuationProbability = CalculateRussianRouletteContinuationProbability(ctx);
            if (RTXDI_GetNextRandom(ptRandContext.initialRandomSamplerState) > continuationProbability)
            {
                return false;
            }
            else
            {
                ctx.MultiplyPathThroughput(1.0/continuationProbability);
            }
            ctx.RecordRussianRouletteProbability(continuationProbability);
        }

        ctx.SetContinuationRay(setupContinuationRay(RAB_GetSurfaceGeoNormal(ctx.GetIntersectionSurface()), RAB_GetSurfaceWorldPos(ctx.GetIntersectionSurface()), ctx.GetBrdfRaySample().outDirection));
    }

    if (!ctx.AnalyzePathReconnectibilityBeforeTrace())
    {
        return false;
    }

    return true;
}

// Starting from a surface, generate an outgoing ray based on the BRDF and calculate the
//  direct lighting at the hit (which becomes indirect lighting at this surface)
template<typename PTContextType>
void RAB_PathTrace(inout RTXDI_PathTracerContext<PTContextType> ctx, inout RTXDI_PathTracerRandomContext ptRandContext, inout RAB_PathTracerUserData ptud)
{
    InitializeDebugPathViz(ctx, ptud);

    uint extraMirrorBounceBudget = g_Const.pt.extraMirrorBounceBudget;

    EnablePSRForPathType(ptud.psr, ptud.pathType, g_Const.updatePSRwithResampling);
    SetPSRPrimarySurfaceDepth(ptud.psr, ctx.GetIntersectionSurface().viewDepth);

    while(ctx.GetBounceDepth() <= ctx.GetMaxPathBounce())
    {
        ctx.BeginPathState();
        bool shouldContinue = DetermineOutgoingRayAndThroughput(ctx, ptRandContext,ptud, extraMirrorBounceBudget);

#if RAB_PATHTRACER_USE_REORDERING == 1
    // Reorder based on whether the loop has terminated
    NvReorderThread(shouldContinue ? 1 : 0, 1);
#endif
        if(!shouldContinue)
            break;

        RAB_Surface prevSurface = ctx.GetIntersectionSurface();

        // TODO: Combine with RecordPathRadiance?
        ctx.SetTraceResult(TraceNextBounce(ctx.GetContinuationRay()));

        if (RAB_IsValidHit(ctx.GetTraceResult()))
        {
            HandleHit(ctx, ptRandContext, prevSurface, ptud.psr);
            if(ctx.IsPathTerminated())
            {
                break;
            }
        }
        else
        {
            ctx.RecordPathRadianceMiss(ptRandContext.initialRandomSamplerState);

            Debug_RecordPTIntersectionPosition(ctx.GetIntersectionSurface().worldPos + DISTANT_LIGHT_DISTANCE * ctx.GetContinuationRay().Direction);

            if (ShouldRecordEnvironmentMapLightSample(ctx))
            {
                float3 envMapRadiance = RAB_GetEnvironmentRadiance(ctx.GetContinuationRay().Direction);
                envMapRadiance *= GetMISWeightForEnvironmentMap(ctx.GetContinuationRay().Direction, prevSurface, ctx.GetBrdfRaySample());

                ctx.RecordEnvironmentMapLightSample(envMapRadiance, prevSurface, ptRandContext.initialRandomSamplerState);
            }
            break;
        }

        ctx.IncreaseBounceDepth();
    }

    FinalizePSRForHybridShift(ptud.psr, ptud.pathType);

    uint finalDepth = min(ctx.GetBounceDepth(), ctx.GetMaxPathBounce());
    Debug_RecordPTFinalVertexCount(finalDepth);
}

#endif // RAB_PATH_TRACING_HLSLI

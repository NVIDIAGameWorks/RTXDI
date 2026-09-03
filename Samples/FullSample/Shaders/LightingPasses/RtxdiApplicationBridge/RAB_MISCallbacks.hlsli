#ifndef RAB_PATH_TRACER_MIS_CALLBACKS
#define RAB_PATH_TRACER_MIS_CALLBACKS

#include "Rtxdi/Utils/BrdfRaySample.hlsli"

// Core NEE MIS: weight = NEEMisTerm / (NEEMisTerm + scatterPdf).
// lightDirection: direction from shading point to light (for env map PDF).
// Returns 1.0 if lightIndex is invalid.
float RAB_GetMISWeightForNEE(
    uint lightIndex,
    const RAB_LightSample lightSample,
    float3 lightDirection,
    float lightSolidAnglePdf,
    float scatterPdf)
{
    // NEE mode has 1.0 weight by definition
    // Emissive mode (aka BRDF mode) should never call this function.
    if (g_Const.pt.lightSamplingMode != PT_INITIAL_SAMPLING_LIGHT_SAMPLING_MODE_MIS)
        return 1.0;
    
    // Analytic lights have NEE weight 1.0 because they are never picked by BRDF sampling
    if (lightIndex == RTXDI_InvalidLightIndex || RAB_IsAnalyticLightSample(lightSample))
        return 1.0;

    RTXDI_DIInitialSamplingParameters sampleParams = g_Const.pt.nee.initialSamplingParams;
    float lightSourcePdf = 0;
    if (lightSample.lightType == PolymorphicLightType::kEnvironment)
        lightSourcePdf = RAB_EvaluateEnvironmentMapSamplingPdf(lightDirection) * lightSolidAnglePdf;
    else // triangle light
    {
        if (sampleParams.localLightSamplingMode == ReSTIRDI_LocalLightSamplingMode_UNIFORM)
            lightSourcePdf = (1.f / g_Const.lightBufferParams.localLightBufferRegion.numLights) * lightSolidAnglePdf;
        else
            lightSourcePdf = RAB_EvaluateLocalLightSourcePdf(lightIndex) * lightSolidAnglePdf;
    }
    uint numSamples = (lightSample.lightType == PolymorphicLightType::kEnvironment ? sampleParams.numEnvironmentSamples : sampleParams.numLocalLightSamples);
    float NEEMisTerm = numSamples * lightSourcePdf;
    return NEEMisTerm / (NEEMisTerm + scatterPdf);
}

// NEE MIS for callers that have RAB_LightSample: applies analytic check and delegates to RTXDI_PT_ApplyMISWeightNEE.
float GetMISWeightForNEELight(
    uint lightIndex,
    RAB_LightSample lightSample,
    float3 shadingWorldPos,
    float scatterPdf)
{
    if (g_Const.pt.lightSamplingMode != PT_INITIAL_SAMPLING_LIGHT_SAMPLING_MODE_MIS)
        return 1.0;

    return RAB_GetMISWeightForNEE(lightIndex, lightSample, normalize(lightSample.position - shadingWorldPos), RAB_LightSampleSolidAnglePdf(lightSample), scatterPdf);
}

// Shared MIS weight computation for PT_INITIAL_SAMPLING_LIGHT_SAMPLING_MODE_MIS.
// Requires g_Const, RAB_*, GetLightIndex, RTXDI_InvalidLightIndex,
// ReSTIRDI_LocalLightSamplingMode_*, and PolymorphicLightType to be in scope (from includer).

// Returns MIS weight for radiance from an emissive surface hit (local/area light).
// Returns 1.0 if the light is not indexed (no MIS applied).
float GetMISWeightForEmissiveSurface(
    RAB_RayPayload rayPayload,
    RAB_Surface prevSurface,
    RTXDI_BrdfRaySample brs)
{
    if (g_Const.pt.lightSamplingMode != PT_INITIAL_SAMPLING_LIGHT_SAMPLING_MODE_MIS)
        return 1.0;
    if (brs.properties.IsDelta())
        return 1.0;
    
    uint lightIndex = GetLightIndex(rayPayload.instanceID, rayPayload.geometryIndex, rayPayload.primitiveIndex);
    if (lightIndex == RTXDI_InvalidLightIndex)
        return 1.0;

    float lightSourcePdf = 0.f;
    RTXDI_DIInitialSamplingParameters sampleParams = g_Const.pt.nee.initialSamplingParams;
    if (sampleParams.localLightSamplingMode == ReSTIRDI_LocalLightSamplingMode_UNIFORM)
        lightSourcePdf = 1.f / g_Const.lightBufferParams.localLightBufferRegion.numLights;
    else
        lightSourcePdf = RAB_EvaluateLocalLightSourcePdf(lightIndex);

    RAB_LightInfo lightInfo = RAB_LoadLightInfo(lightIndex, false);
    RAB_LightSample lightSample = RAB_SamplePolymorphicLight(lightInfo, prevSurface, rayPayload.barycentrics);
    float lightSolidAnglePdf = RAB_LightSampleSolidAnglePdf(lightSample);
    lightSourcePdf *= lightSolidAnglePdf;

    float pdfSum = sampleParams.numLocalLightSamples * lightSourcePdf + brs.outPdf;
    return brs.outPdf / pdfSum;
}

// Returns MIS weight for radiance from environment map (miss / distant light).
float GetMISWeightForEnvironmentMap(float3 direction, RAB_Surface prevSurface, RTXDI_BrdfRaySample brs)
{
    if (g_Const.pt.lightSamplingMode != PT_INITIAL_SAMPLING_LIGHT_SAMPLING_MODE_MIS)
        return 1.0;
    if(brs.properties.IsDelta())
        return 1.0;

    RTXDI_DIInitialSamplingParameters sampleParams = g_Const.pt.nee.initialSamplingParams;
    float lightSourcePdf = RAB_EvaluateEnvironmentMapSamplingPdf(direction);
    int lightIndex = g_Const.lightBufferParams.environmentLightParams.lightIndex;
    RAB_LightInfo lightInfo = RAB_LoadLightInfo(lightIndex, false);
    float2 randXY = RAB_GetEnvironmentMapRandXYFromDir(direction);
    RAB_LightSample lightSample = RAB_SamplePolymorphicLight(lightInfo, prevSurface, randXY);
    float lightSolidAnglePdf = RAB_LightSampleSolidAnglePdf(lightSample);
    lightSourcePdf *= lightSolidAnglePdf;

    float pdfSum = sampleParams.numEnvironmentSamples * lightSourcePdf + brs.outPdf;
    return brs.outPdf / pdfSum;
}

#endif // RAB_PATH_TRACER_MIS_CALLBACKS

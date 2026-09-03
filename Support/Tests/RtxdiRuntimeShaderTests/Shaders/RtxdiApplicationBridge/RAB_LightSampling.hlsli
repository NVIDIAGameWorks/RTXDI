#ifndef RAB_LIGHT_SAMPLING_HLSLI
#define RAB_LIGHT_SAMPLING_HLSLI

#include "RAB_RayPayload.hlsli"

float2 RAB_GetEnvironmentMapRandXYFromDir(float3 worldDir)
{
    return float2(0.0, 0.0);
}

float RAB_EvaluateEnvironmentMapSamplingPdf(float3 L)
{
    return 0.0;
}

float RAB_EvaluateLocalLightSourcePdf(uint lightIndex)
{
    return 0.0;
}

float3 RAB_GetReflectedRadianceForSurface(float3 incomingRadianceLocation, float3 incomingRadiance, RAB_Surface surface)
{
    return float3(0.0, 0.0, 0.0);
}

float RAB_GetReflectedLuminanceForSurface(float3 incomingRadianceLocation, float3 incomingRadiance, RAB_Surface surface)
{
    return 0.0;
}

float RAB_GetLightSampleTargetPdfForSurface(RAB_LightSample lightSample, RAB_Surface surface)
{
    return 0.0;
}

float RAB_GetGISampleTargetPdfForSurface(float3 samplePosition, float3 sampleRadiance, RAB_Surface surface)
{
    return 0.0;
}

float3 RAB_GetPTSampleTargetPdfForSurface(float3 samplePosition, float3 sampleRadiance, RAB_Surface surface)
{
    return float3(0.0, 0.0, 0.0);
}

void RAB_GetLightDirDistance(RAB_Surface surface, RAB_LightSample lightSample,
    out float3 o_lightDir,
    out float o_lightDistance)
{
    o_lightDir = float3(0.0, 0.0, 0.0);
    o_lightDistance = 0;
}

bool RTXDI_CompareRelativeDifference(float reference, float candidate, float threshold);

float3 GetEnvironmentRadiance(float3 direction)
{
    return float3(0.0, 0.0, 0.0);
}

bool IsComplexSurface(int2 pixelPosition, RAB_Surface surface)
{
    return true;
}

uint GetLightIndex(uint instanceID, uint geometryIndex, uint primitiveIndex)
{
    return 0;
}

bool RAB_TraceRayForLocalLight(float3 origin, float3 direction, float tMin, float tMax,
    out uint o_lightIndex, out float2 o_randXY)
{
	o_lightIndex = 0;
	o_randXY = float2(0.0f, 0.0f);
    return true;
}

#endif // RAB_LIGHT_SAMPLING_HLSLI

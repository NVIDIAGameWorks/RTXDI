#ifndef RAB_LIGHT_SAMPLING_HLSLI
#define RAB_LIGHT_SAMPLING_HLSLI

#include "RAB_Material.hlsli"
#include "RAB_RayPayload.hlsli"
#include "RAB_Surface.hlsli"

float2 RAB_GetEnvironmentMapRandXYFromDir(float3 worldDir)
{
    return float2(0.0, 0.0);
}

float RAB_EvaluateEnvironmentMapSamplingPdf(float3 L)
{
    // No Environment sampling
    return 0;
}

float RAB_EvaluateLocalLightSourcePdf(uint lightIndex)
{
    // Uniform pdf
    return 1.0 / g_Const.lightBufferParams.localLightBufferRegion.numLights;
}

float3 RAB_GetReflectedRadianceForSurface(float3 incomingRadianceLocation, float3 incomingRadiance, RAB_Surface surface)
{
    return float3(0.0, 0.0, 0.0);
}

float RAB_GetReflectedLuminanceForSurface(float3 incomingRadianceLocation, float3 incomingRadiance, RAB_Surface surface)
{
    return 0.0;
}

// Evaluate the surface BRDF and compute the weighted reflected radiance for the given light sample
float3 ShadeSurfaceWithLightSample(RAB_LightSample lightSample, RAB_Surface surface)
{
    // Ignore invalid light samples
    if (lightSample.solidAnglePdf <= 0)
        return 0;

    float3 L = normalize(lightSample.position - surface.worldPos);

    // Ignore light samples that are below the geometric surface (but above the normal mapped surface)
    if (dot(L, surface.geoNormal) <= 0)
        return 0;


    float3 V = surface.viewDir;
    
    // Evaluate the BRDF
    float diffuse = Lambert(surface.normal, -L);
    float3 specular = GGX_times_NdotL(V, L, surface.normal, max(RAB_GetMaterial(surface).roughness, kMinRoughness), RAB_GetMaterial(surface).specularF0);

    float3 reflectedRadiance = lightSample.radiance * (diffuse * surface.material.diffuseAlbedo + specular);

    return reflectedRadiance / lightSample.solidAnglePdf;
}

// Compute the target PDF (p-hat) for the given light sample relative to a surface
float RAB_GetLightSampleTargetPdfForSurface(RAB_LightSample lightSample, RAB_Surface surface)
{
    // Second-best implementation: the PDF is proportional to the reflected radiance.
    // The best implementation would be taking visibility into account,
    // but that would be prohibitively expensive.
    return CalcLuminance(ShadeSurfaceWithLightSample(lightSample, surface));
}

float RAB_GetGISampleTargetPdfForSurface(float3 samplePosition, float3 sampleRadiance, RAB_Surface surface)
{
    return 0.0;
}

void RAB_GetLightDirDistance(RAB_Surface surface, RAB_LightSample lightSample,
    out float3 o_lightDir,
    out float o_lightDistance)
{
    float3 toLight = lightSample.position - surface.worldPos;
    o_lightDistance = length(toLight);
    o_lightDir = toLight / o_lightDistance;
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
    uint lightIndex = RTXDI_InvalidLightIndex;
    InstanceData hitInstance = t_InstanceData[instanceID];
    uint geometryInstanceIndex = hitInstance.firstGeometryInstanceIndex + geometryIndex;
    lightIndex = t_GeometryInstanceToLight[geometryInstanceIndex];
    if (lightIndex != RTXDI_InvalidLightIndex)
      lightIndex += primitiveIndex;
    return lightIndex;
}

// Return true if anything was hit. If false, RTXDI will do environment map sampling
// o_lightIndex: If hit, must be a valid light index for RAB_LoadLightInfo, if no local light was hit, must be RTXDI_InvalidLightIndex
// randXY: The randXY that corresponds to the hit location and is the same used for RAB_SamplePolymorphicLight
bool RAB_TraceRayForLocalLight(float3 origin, float3 direction, float tMin, float tMax,
    out uint o_lightIndex, out float2 o_randXY)
{
    o_lightIndex = RTXDI_InvalidLightIndex;
    o_randXY = 0;

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = tMin;
    ray.TMax = tMax;

    RayQuery<RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> rayQuery;
    rayQuery.TraceRayInline(SceneBVH, RAY_FLAG_NONE, INSTANCE_MASK_OPAQUE, ray);
    rayQuery.Proceed();

    bool hitAnything = rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    if (hitAnything)
    {
        o_lightIndex = GetLightIndex(rayQuery.CommittedInstanceID(), rayQuery.CommittedGeometryIndex(), rayQuery.CommittedPrimitiveIndex());
        if (o_lightIndex != RTXDI_InvalidLightIndex)
        {
            float2 hitUV = rayQuery.CommittedTriangleBarycentrics();
            o_randXY = RandomFromBarycentric(HitUVToBarycentric(hitUV));
        }
    }

    return hitAnything;
}

// Compute the position on a triangle light given a pair of random numbers
RAB_LightSample RAB_SamplePolymorphicLight(RAB_LightInfo lightInfo, RAB_Surface surface, float2 uv)
{
    TriangleLight triLight = TriangleLight::Create(lightInfo);
    return CalcSample(triLight, uv, surface.worldPos);
}

#endif // RAB_LIGHT_SAMPLING_HLSLI

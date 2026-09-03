#ifndef RTXDI_RAB_LIGHT_INFO_HLSLI
#define RTXDI_RAB_LIGHT_INFO_HLSLI

#include "../TriangleLight.hlsli"

struct RAB_LightSample
{
    float3 position;
    float3 normal;
    float3 radiance;
    float solidAnglePdf;
};

RAB_LightSample RAB_EmptyLightSample()
{
    return (RAB_LightSample)0;
}

bool RAB_IsAnalyticLightSample(RAB_LightSample lightSample)
{
    return false;
}

float RAB_LightSampleSolidAnglePdf(RAB_LightSample lightSample)
{
    return lightSample.solidAnglePdf;
}

RAB_LightSample CalcSample(TriangleLight triLight, in const float2 random, in const float3 viewerPosition)
{
    RAB_LightSample result;

    float3 bary = SampleTriangle(random);
    result.position = triLight.base + triLight.edge1 * bary.y + triLight.edge2 * bary.z;
    result.normal = triLight.normal;

    result.solidAnglePdf = triLight.CalcSolidAnglePdf(viewerPosition, result.position, result.normal);

    result.radiance = triLight.radiance;

    return result;   
}

#endif // RTXDI_RAB_LIGHT_INFO_HLSLI

#ifndef RAB_LIGHT_INFO_HLSLI
#define RAB_LIGHT_INFO_HLSLI

#include "RAB_Surface.hlsli"
#include "RAB_LightSample.hlsli"

#define RAB_DISTANT_LIGHT_DISTANCE 0

struct RAB_LightInfo
{
    uint unused;
};

RAB_LightInfo RAB_EmptyLightInfo()
{
    RAB_LightInfo lightInfo;
    return lightInfo;
}

RAB_LightInfo RAB_LoadLightInfo(uint index, bool prevFrame)
{
    return RAB_EmptyLightInfo();
}

RAB_LightInfo RAB_LoadCompactLightInfo(uint linearIndex)
{
    return RAB_EmptyLightInfo();
}

bool RAB_StoreCompactLightInfo(uint linearIndex, RAB_LightInfo lightInfo)
{
    return true;
}

float RAB_GetLightTargetPdfForVolume(RAB_LightInfo light, float3 volumeCenter, float volumeRadius)
{
    return 0.0;
}

RAB_LightSample RAB_SamplePolymorphicLight(RAB_LightInfo lightInfo, RAB_Surface surface, float2 uv)
{
    return RAB_EmptyLightSample();
}

#endif // RAB_LIGHT_INFO_HLSLI

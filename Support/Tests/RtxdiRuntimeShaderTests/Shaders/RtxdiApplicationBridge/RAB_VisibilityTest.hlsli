#ifndef RAB_VISIBILITY_TEST_HLSLI
#define RAB_VISIBILITY_TEST_HLSLI

bool RAB_GetConservativeVisibility(RAB_Surface surface, RAB_LightSample lightSample)
{
    return true;
}

bool RAB_GetTemporalConservativeVisibility(RAB_Surface currentSurface, RAB_Surface prevSurface, RAB_LightSample lightSample)
{
    return true;
}

bool RAB_GetConservativeVisibility(RAB_Surface surface, float3 samplePosition)
{
    return true;
}

bool RAB_GetTemporalConservativeVisibility(RAB_Surface currentSurface, RAB_Surface prevSurface, float3 samplePosition)
{
    return true;
}

#endif // RAB_VISIBILITY_TEST_HLSLI

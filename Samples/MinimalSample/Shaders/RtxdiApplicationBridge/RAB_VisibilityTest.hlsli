#ifndef RAB_VISIBILITY_TEST_HLSLI
#define RAB_VISIBILITY_TEST_HLSLI

RayDesc setupVisibilityRay(RAB_Surface surface, RAB_LightSample lightSample, float offset = 0.001)
{
    float3 L = lightSample.position - surface.worldPos;

    RayDesc ray;
    ray.TMin = offset;
    ray.TMax = length(L) - offset;
    ray.Direction = normalize(L);
    ray.Origin = surface.worldPos;

    return ray;
}

// Tests the visibility between a surface and a light sample.
// Returns true if there is nothing between them.
bool RAB_GetConservativeVisibility(RAB_Surface surface, RAB_LightSample lightSample)
{
    RayDesc ray = setupVisibilityRay(surface, lightSample);

    RayQuery<RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> rayQuery;

    rayQuery.TraceRayInline(SceneBVH, RAY_FLAG_NONE, INSTANCE_MASK_OPAQUE, ray);

    rayQuery.Proceed();

    bool visible = (rayQuery.CommittedStatus() == COMMITTED_NOTHING);
    
    return visible;
}

// Tests the visibility between a surface and a light sample on the previous frame.
// Since the scene is static in this sample app, it's equivalent to RAB_GetConservativeVisibility.
bool RAB_GetTemporalConservativeVisibility(RAB_Surface curSurface, RAB_Surface prevSurface,
    RAB_LightSample lightSample)
{
    return RAB_GetConservativeVisibility(curSurface, lightSample);
}

bool RAB_GetConservativeVisibility(RAB_Surface surface, float3 samplePosition)
{
    return true;
}

bool RAB_GetTemporalConservativeVisibility(RAB_Surface curSurface, RAB_Surface prevSurface, float3 samplePosition)
{
    return true;
}

#endif // RAB_VISIBILITY_TEST_HLSLI

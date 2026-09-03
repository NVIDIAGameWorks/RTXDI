#ifndef RTXDI_RAB_SURFACE_HLSLI
#define RTXDI_RAB_SURFACE_HLSLI

#include "Rtxdi/Utils/RandomSamplerState.hlsli"
#include "RAB_Material.hlsli"

struct RAB_Surface
{
    uint unused;
};

RAB_Surface RAB_EmptySurface()
{
    RAB_Surface surface;
    return surface;
}

bool RAB_IsSurfaceValid(RAB_Surface surface)
{
    return true;
}

float3 RAB_GetSurfaceWorldPos(RAB_Surface surface)
{
    return float3(0.0, 0.0, 0.0);
}

void RAB_SetSurfaceWorldPos(inout RAB_Surface surface, float3 worldPos)
{

}

float RAB_GetSurfaceRoughness(RAB_Surface surface)
{
    return 0.0;
}

float3 RAB_GetSurfaceViewDir(RAB_Surface surfacE)
{
    return float3(0.0, 0.0, 0.0);
}

RAB_Material RAB_GetMaterial(RAB_Surface surface)
{
    return RAB_EmptyMaterial();
}

float3 RAB_GetSurfaceNormal(RAB_Surface surface)
{
    return float3(0.0, 0.0, 0.0);
}

void RAB_SetSurfaceNormal(inout RAB_Surface surface, float3 normal)
{

}

float RAB_GetSurfaceLinearDepth(RAB_Surface surface)
{
    return 0.0;
}

RAB_Surface RAB_GetGBufferSurface(int2 pixelPosition, bool prevFrame)
{
    return RAB_EmptySurface();
}

bool RAB_GetSurfaceBrdfSample(RAB_Surface surface, inout RTXDI_RandomSamplerState rng, out float3 dir)
{
    dir = float3(0.0, 0.0, 0.0);
    return false;
}

float RAB_GetSurfaceBrdfPdf(RAB_Surface surface, float3 dir)
{
    return 0.0;
}

float RAB_SurfaceEvaluateBrdfPdf(RAB_Surface, float3 outDir)
{
	return 0.0;
}

bool RAB_SurfaceImportanceSampleBrdf(RAB_Surface surface, inout RTXDI_RandomSamplerState rng, out float3 sampleDir)
{
	return false;
}

float3 RAB_GetReflectedBsdfRadianceForSurface(float3 incomingRadianceLocation, float3 incomingRadiance, RAB_Surface surface)
{
    return float3(0.0, 0.0, 0.0);
}

#endif // RTXDI_RAB_SURFACE_HLSLI

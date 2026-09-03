#ifndef RTXDI_RAB_SURFACE_HLSLI
#define RTXDI_RAB_SURFACE_HLSLI

#include "../GBufferHelpers.hlsli"

#include "Rtxdi/Utils/RandomSamplerState.hlsli"
#include "RAB_Material.hlsli"

// A surface with enough information to evaluate BRDFs
struct RAB_Surface
{
    float3 worldPos;
    float3 viewDir;
    float viewDepth;
    float3 normal;
    float3 geoNormal;
    float diffuseProbability;
    RAB_Material material;
};

RAB_Surface RAB_EmptySurface()
{
    RAB_Surface surface = (RAB_Surface)0;
    surface.viewDepth = BACKGROUND_DEPTH;
    return surface;
}

bool RAB_IsSurfaceValid(RAB_Surface surface)
{
    return surface.viewDepth != BACKGROUND_DEPTH;
}

float3 RAB_GetSurfaceWorldPos(RAB_Surface surface)
{
    return surface.worldPos;
}

RAB_Material RAB_GetMaterial(RAB_Surface surface)
{
    return surface.material;
}

float3 RAB_GetSurfaceNormal(RAB_Surface surface)
{
    return surface.normal;
}

float RAB_GetSurfaceLinearDepth(RAB_Surface surface)
{
    return surface.viewDepth;
}

float GetSurfaceDiffuseProbability(RAB_Surface surface)
{
    RAB_Material material = RAB_GetMaterial(surface);
    float diffuseWeight = CalcLuminance(material.diffuseAlbedo);
    float specularWeight = CalcLuminance(Schlick_Fresnel(material.specularF0, dot(surface.viewDir, surface.normal)));
    float sumWeights = diffuseWeight + specularWeight;
    return sumWeights < 1e-7f ? 1.f : (diffuseWeight / sumWeights);
}

// Load a sample from the previous G-buffer.
RAB_Surface RAB_GetGBufferSurface(int2 pixelPosition, bool prevFrame)
{
    RAB_Surface surface = RAB_EmptySurface();

    // We do not have access to the current G-buffer in this sample because it's using
    // a single render pass with a fused resampling kernel, so just return an invalid surface.
    // This should never happen though, as the fused kernel doesn't call RAB_GetGBufferSurface(..., false)
    if (!prevFrame)
        return surface;

    const PlanarViewConstants view = g_Const.prevView;

    if (any(pixelPosition >= view.viewportSize))
        return surface;

    surface.viewDepth = t_PrevGBufferDepth[pixelPosition];

    if(surface.viewDepth == BACKGROUND_DEPTH)
        return surface;

    surface.normal = octToNdirUnorm32(t_PrevGBufferNormals[pixelPosition]);
    surface.geoNormal = octToNdirUnorm32(t_PrevGBufferGeoNormals[pixelPosition]);
    float4 specularRough = Unpack_R8G8B8A8_Gamma_UFLOAT(t_PrevGBufferSpecularRough[pixelPosition]);
    surface.material = RAB_GetGBufferMaterial(pixelPosition, view, u_GBufferDiffuseAlbedo, u_GBufferSpecularRough);
    surface.worldPos = ViewDepthToWorldPos(view, pixelPosition, surface.viewDepth);
    surface.viewDir = normalize(g_Const.view.cameraDirectionOrPosition.xyz - surface.worldPos);
    surface.diffuseProbability = GetSurfaceDiffuseProbability(surface);

    return surface;
}

float3 WorldToTangent(RAB_Surface surface, float3 w)
{
    // reconstruct tangent frame based off worldspace normal
    // this is ok for isotropic BRDFs
    // for anisotropic BRDFs, we need a user defined tangent
    float3 tangent;
    float3 bitangent;
    ConstructONB(surface.normal, tangent, bitangent);

    return float3(dot(bitangent, w), dot(tangent, w), dot(surface.normal, w));
}

float3 TangentToWorld(RAB_Surface surface, float3 h)
{
    // reconstruct tangent frame based off worldspace normal
    // this is ok for isotropic BRDFs
    // for anisotropic BRDFs, we need a user defined tangent
    float3 tangent;
    float3 bitangent;
    ConstructONB(surface.normal, tangent, bitangent);

    return bitangent * h.x + tangent * h.y + surface.normal * h.z;
}

// Output an importanced sampled reflection direction from the BRDF given the view
// Return true if the returned direction is above the surface
bool RAB_SurfaceImportanceSampleBrdf(RAB_Surface surface, inout RTXDI_RandomSamplerState rng, out float3 dir)
{
    float3 rand;
    rand.x = RTXDI_GetNextRandom(rng);
    rand.y = RTXDI_GetNextRandom(rng);
    rand.z = RTXDI_GetNextRandom(rng);
    if (rand.x < surface.diffuseProbability)
    {
        float pdf;
        float3 h = SampleCosHemisphere(rand.yz, pdf);
        dir = TangentToWorld(surface, h);
    }
    else
    {
        float3 h = ImportanceSampleGGX(rand.yz, max(surface.material.roughness, kMinRoughness));
        dir = reflect(-surface.viewDir, TangentToWorld(surface, h));
    }

    return dot(surface.normal, dir) > 0.f;
}

// Return PDF wrt solid angle for the BRDF in the given dir
float RAB_SurfaceEvaluateBrdfPdf(RAB_Surface surface, float3 dir)
{
    float cosTheta = saturate(dot(surface.normal, dir));
    float diffusePdf = cosTheta / M_PI;
    float specularPdf = ImportanceSampleGGX_VNDF_PDF(max(surface.material.roughness, kMinRoughness), surface.normal, surface.viewDir, dir);
    float pdf = cosTheta > 0.f ? lerp(specularPdf, diffusePdf, surface.diffuseProbability) : 0.f;
    return pdf;
}

#endif // RTXDI_RAB_SURFACE_HLSLI

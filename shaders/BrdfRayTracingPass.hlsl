/***************************************************************************
 # Copyright (c) 2020-2021, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma pack_matrix(row_major)

#include "RtxdiApplicationBridge.hlsli"

#include <rtxdi/ResamplingFunctions.hlsli>

#ifdef WITH_NRD
#define COMPILER_DXC
#include <NRD.hlsl>
#endif

#include "ShadingHelpers.hlsli"

static const float c_MaxIndirectRadiance = 10;

#if USE_RAY_QUERY
[numthreads(16, 16, 1)]
void main(uint2 GlobalIndex : SV_DispatchThreadID)
#else
[shader("raygeneration")]
void RayGen()
#endif
{
#if !USE_RAY_QUERY
    uint2 GlobalIndex = DispatchRaysIndex().xy;
#endif
    uint2 pixelPosition = RTXDI_ReservoirToPixelPos(GlobalIndex, g_Const.runtimeParams);

    RAB_RandomSamplerState rng = RAB_InitRandomSampler(GlobalIndex, 5);
    RAB_RandomSamplerState tileRng = RAB_InitRandomSampler(GlobalIndex / RTXDI_TILE_SIZE_IN_PIXELS, 1);

    RAB_Surface surface = RAB_GetGBufferSurface(pixelPosition, false);
    
    float3 tangent, bitangent;
    branchlessONB(surface.normal, tangent, bitangent);

    float distance = max(1, 0.1 * length(surface.worldPos - g_Const.view.cameraDirectionOrPosition.xyz));

    RayDesc ray;
    ray.TMin = 0.001f * distance;
    ray.TMax = 1000;

    float2 Rand;
    Rand.x = RAB_GetNextRandom(rng);
    Rand.y = RAB_GetNextRandom(rng);

    float3 V = normalize(g_Const.view.cameraDirectionOrPosition.xyz - surface.worldPos);

    float3 specularDirection;
    float3 specular_BRDF_over_PDF;
    {
        float3 Ve = float3(dot(V, tangent), dot(V, bitangent), dot(V, surface.normal));
        float3 He = sampleGGX_VNDF(Ve, surface.roughness, Rand);
        float3 H = normalize(He.x * tangent + He.y * bitangent + He.z * surface.normal);
        specularDirection = reflect(-V, H);

        float HoV = saturate(dot(H, V));
        float NoV = saturate(dot(surface.normal, V));
        float3 F = Schlick_Fresnel(surface.specularF0, HoV);
        float G1 = (NoV > 0) ? G1_Smith(surface.roughness, NoV) : 0;
        specular_BRDF_over_PDF = F * G1;
    }

    float3 diffuseDirection;
    float diffuse_BRDF_over_PDF;
    {
        float solidAnglePdf;
        float3 localDirection = sampleCosHemisphere(Rand, solidAnglePdf);
        diffuseDirection = tangent * localDirection.x + bitangent * localDirection.y + surface.normal * localDirection.z;
        diffuse_BRDF_over_PDF = 1.0;
    }

    float specular_PDF = saturate(calcLuminance(specular_BRDF_over_PDF) /
        calcLuminance(specular_BRDF_over_PDF + diffuse_BRDF_over_PDF * surface.diffuseAlbedo));

    if (g_Const.enableBrdfMIS && !g_Const.enableBrdfIndirect)
        specular_PDF = 1.0;

    bool isSpecularRay = RAB_GetNextRandom(rng) < specular_PDF;

    float3 BRDF_over_PDF;
    if (isSpecularRay)
    {
        ray.Direction = specularDirection;
        BRDF_over_PDF = specular_BRDF_over_PDF / specular_PDF;
    }
    else
    {
        ray.Direction = diffuseDirection;
        BRDF_over_PDF = diffuse_BRDF_over_PDF / (1.0 - specular_PDF);
    }

    if (dot(surface.geoNormal, ray.Direction) <= 0.0)
    {
        BRDF_over_PDF = 0.0;
        ray.TMax = 0;
    }

    ray.Origin = surface.worldPos;

    float3 radiance = 0;
    
    RayPayload payload = (RayPayload)0;
    payload.instanceID = ~0u;
    payload.throughput = 1.0;

#if USE_RAY_QUERY
    RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> rayQuery;
    
    rayQuery.TraceRayInline(SceneBVH, RAY_FLAG_NONE, INSTANCE_MASK_ALL, ray);

    while (rayQuery.Proceed())
    {
        if (rayQuery.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            if (considerTransparentMaterial(
                rayQuery.CandidateInstanceID(),
                rayQuery.CandidateGeometryIndex(),
                rayQuery.CandidatePrimitiveIndex(),
                rayQuery.CandidateTriangleBarycentrics(),
                payload.throughput))
            {
                rayQuery.CommitNonOpaqueTriangleHit();
            }
        }
    }

    if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        payload.instanceID = rayQuery.CommittedInstanceID();
        payload.geometryIndex = rayQuery.CommittedGeometryIndex();
        payload.primitiveIndex = rayQuery.CommittedPrimitiveIndex();
        payload.barycentrics = rayQuery.CommittedTriangleBarycentrics();
        payload.committedRayT = rayQuery.CommittedRayT();
    }
#else
    TraceRay(SceneBVH, RAY_FLAG_NONE, INSTANCE_MASK_ALL, 0, 0, 0, ray, payload);
#endif

    if (g_PerPassConstants.rayCountBufferIndex >= 0)
    {
        InterlockedAdd(u_RayCountBuffer[RAY_COUNT_TRACED(g_PerPassConstants.rayCountBufferIndex)], 1);
    }

    if (payload.instanceID != ~0u)
    {
        if (g_PerPassConstants.rayCountBufferIndex >= 0)
        {
            InterlockedAdd(u_RayCountBuffer[RAY_COUNT_HITS(g_PerPassConstants.rayCountBufferIndex)], 1);
        }

        GeometrySample gs = getGeometryFromHit(
            payload.instanceID,
            payload.geometryIndex,
            payload.primitiveIndex,
            payload.barycentrics,
            GeomAttr_Normal | GeomAttr_TexCoord | GeomAttr_Position,
            t_InstanceData, t_GeometryData, t_MaterialConstants);
        
        MaterialSample ms = sampleGeometryMaterial(gs, 0, 0, 0,
            MatAttr_BaseColor | MatAttr_Emissive | MatAttr_MetalRough, s_MaterialSampler);

        if (g_Const.enableBrdfMIS)
        {
            if (isSpecularRay && any(ms.emissiveColor > 0))
            {
                float3 worldSpacePositions[3];
                worldSpacePositions[0] = mul(gs.instance.transform, float4(gs.vertexPositions[0], 1.0)).xyz;
                worldSpacePositions[1] = mul(gs.instance.transform, float4(gs.vertexPositions[1], 1.0)).xyz;
                worldSpacePositions[2] = mul(gs.instance.transform, float4(gs.vertexPositions[2], 1.0)).xyz;

                float3 triangleNormal = cross(
                    worldSpacePositions[1] - worldSpacePositions[0],
                    worldSpacePositions[2] - worldSpacePositions[0]);

                float area = 0.5 * length(triangleNormal);
                triangleNormal = normalize(triangleNormal);

                float solidAnglePdf = pdfAtoW(1.0 / area, payload.committedRayT, abs(dot(ray.Direction, triangleNormal)));

                float misWeight = 1.0 - EvaluateSpecularSampledLightingWeight(surface, ray.Direction, solidAnglePdf);

                radiance += ms.emissiveColor * misWeight;
            }
        }
        else
        {
            radiance += ms.emissiveColor;
        }

        if (g_Const.enableBrdfIndirect)
        {
            RAB_Surface secondarySurface;
            secondarySurface.worldPos = ray.Origin + ray.Direction * payload.committedRayT;
            secondarySurface.viewDepth = 1.0; // don't care
            secondarySurface.normal = (dot(gs.geometryNormal, ray.Direction) < 0) ? gs.geometryNormal : -gs.geometryNormal;
            secondarySurface.geoNormal = secondarySurface.normal;
            secondarySurface.diffuseAlbedo = ms.diffuseAlbedo;
            secondarySurface.specularF0 = ms.specularF0;
            secondarySurface.roughness = ms.roughness;

            const RTXDI_ResamplingRuntimeParameters params = g_Const.runtimeParams;

            RAB_LightSample lightSample;
            RTXDI_Reservoir reservoir = RTXDI_SampleLightsForSurface(rng, tileRng, secondarySurface,
                g_Const.numIndirectRegirSamples, 
                g_Const.numIndirectLocalLightSamples, 
                g_Const.numIndirectInfiniteLightSamples, 
                g_Const.numIndirectEnvironmentSamples,
                params, u_RisBuffer, lightSample);

            float lightSampleScale = (lightSample.solidAnglePdf > 0) ? RTXDI_GetReservoirInvPdf(reservoir) / lightSample.solidAnglePdf : 0;

            // Firefly suppression
            float indirectLuminance = calcLuminance(lightSample.radiance) * lightSampleScale;
            if(indirectLuminance > c_MaxIndirectRadiance)
                lightSampleScale *= c_MaxIndirectRadiance / indirectLuminance;

            float3 indirectDiffuse = 0;
            float3 indirectSpecular = 0;
            float lightDistance = 0;
            ShadeSurfaceWithLightSample(reservoir, secondarySurface, lightSample, indirectDiffuse, indirectSpecular, lightDistance);
            
            radiance += indirectDiffuse * ms.diffuseAlbedo + indirectSpecular;
        }
    }
    else if (g_Const.enableEnvironmentMap && (isSpecularRay || !g_Const.enableBrdfMIS) && calcLuminance(BRDF_over_PDF) > 0.0)
    {
        Texture2D environmentLatLongMap = t_BindlessTextures[g_Const.environmentMapTextureIndex];

        float2 uv = directionToEquirectUV(ray.Direction);
        uv.x -= g_Const.environmentRotation;

        float3 environmentRadiance = environmentLatLongMap.SampleLevel(s_EnvironmentSampler, uv, 0).rgb;
        environmentRadiance *= g_Const.environmentScale;

        if (g_Const.enableBrdfMIS)
        {
            float solidAnglePdf;
            if (g_Const.environmentMapImportanceSampling)
            { 
                // Uniform sampling of the environment sphere
                solidAnglePdf = 0.25 / c_pi;
            }
            else
            {
                // Matches the expression in EnvironmentLight::calcSample
                float sinElevation = ray.Direction.y;
                float cosElevation = sqrt(saturate(1.0 - square(sinElevation)));
                solidAnglePdf = 1.0 / (2 * c_pi * c_pi * cosElevation);
            }

            solidAnglePdf *= EvaluateEnvironmentMapSamplingPdf(ray.Direction);
    
            float misWeight = 1.0 - EvaluateSpecularSampledLightingWeight(surface, ray.Direction, solidAnglePdf);
            environmentRadiance *= misWeight;
        }

        radiance += environmentRadiance;
    }

    radiance *= payload.throughput;

    float3 diffuse = isSpecularRay ? 0.0 : radiance * BRDF_over_PDF;
    float3 specular = isSpecularRay ? radiance * BRDF_over_PDF : 0.0;
    float diffuseHitT = payload.committedRayT;
    float specularHitT = payload.committedRayT;

    specular = DemodulateSpecular(surface, specular);

    uint2 lightingTexturePos = (g_Const.denoiserMode == DENOISER_MODE_REBLUR)
        ? GlobalIndex
        : pixelPosition;

    if (g_Const.denoiserMode != DENOISER_MODE_REBLUR && g_Const.runtimeParams.activeCheckerboardField != 0)
    {
        diffuse *= 2;
        specular *= 2;

        int2 otherFieldPixelPosition = pixelPosition;
        otherFieldPixelPosition.x += (g_Const.runtimeParams.activeCheckerboardField == 1) == ((pixelPosition.y & 1) != 0)
            ? 1 : -1;

        u_DiffuseLighting[otherFieldPixelPosition] = 0;
        u_SpecularLighting[otherFieldPixelPosition] = 0;
    }

    if (g_Const.enableBrdfAdditiveBlend)
    {
        float4 restirDiffuse = u_DiffuseLighting[lightingTexturePos];
        float4 restirSpecular = u_SpecularLighting[lightingTexturePos];

        diffuse += restirDiffuse.rgb;
        specular += restirSpecular.rgb;

        if(isSpecularRay)
            diffuseHitT = restirDiffuse.w;
        else
            specularHitT = restirSpecular.w;
    }

#if WITH_NRD
    if(g_Const.denoiserMode != DENOISER_MODE_OFF && g_Const.enableDenoiserInputPacking)
    {
        const bool useReLAX = (g_Const.denoiserMode == DENOISER_MODE_RELAX);
         
        if (useReLAX)
        {
            u_DiffuseLighting[lightingTexturePos] = RELAX_FrontEnd_PackRadiance(diffuse, diffuseHitT);
            u_SpecularLighting[lightingTexturePos] = RELAX_FrontEnd_PackRadiance(specular, specularHitT);
        }
        else
        {
            float diffNormDist = REBLUR_FrontEnd_GetNormHitDist(diffuseHitT, surface.viewDepth, g_Const.reblurDiffHitDistParams);
            u_DiffuseLighting[lightingTexturePos] = REBLUR_FrontEnd_PackRadiance(diffuse, diffNormDist);
            
            float specNormDist = REBLUR_FrontEnd_GetNormHitDist(specularHitT, surface.viewDepth, g_Const.reblurSpecHitDistParams, surface.roughness);
            u_SpecularLighting[lightingTexturePos] = REBLUR_FrontEnd_PackRadiance(specular, specNormDist);
        }
    }
    else
#endif
    {
        u_DiffuseLighting[lightingTexturePos] = float4(diffuse, diffuseHitT);
        u_SpecularLighting[lightingTexturePos] = float4(specular, specularHitT);
    }
}

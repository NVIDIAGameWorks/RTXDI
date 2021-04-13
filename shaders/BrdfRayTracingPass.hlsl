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
#include <NRD.hlsl>
#endif

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

    float F0 = lerp(0.05, 1.0, surface.metalness);
    float3 V = normalize(g_Const.view.cameraDirectionOrPosition.xyz - surface.worldPos);

    float surfaceFresnel = Schlick_Fresnel(F0, saturate(dot(surface.normal, V)));

    bool isSpecularRay = RAB_GetNextRandom(rng) < surfaceFresnel;
    float BRDF_over_PDF;

    if(isSpecularRay)
    {
        float3 Ve = float3(dot(V, tangent), dot(V, bitangent), dot(V, surface.normal));
        float3 Ne = sampleGGX_VNDF(Ve, surface.roughness, Rand);
        float3 N = normalize(Ne.x * tangent + Ne.y * bitangent + Ne.z * surface.normal);
        ray.Direction = reflect(-V, N);

        float NoV = saturate(dot(N, V));
        float F = Schlick_Fresnel(F0, NoV);
        float G1 = (NoV > 0) ? G1_Smith(surface.roughness, NoV) : 0;
        BRDF_over_PDF = F * G1;
        BRDF_over_PDF *= 1.0 / surfaceFresnel;
    }
    else
    {
        float solidAnglePdf;
        float3 localDirection = sampleCosHemisphere(Rand, solidAnglePdf);
        ray.Direction = tangent * localDirection.x + bitangent * localDirection.y + surface.normal * localDirection.z;
        BRDF_over_PDF = c_pi;
        BRDF_over_PDF *= 1.0 / (1.0 - surfaceFresnel);
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
            payload.primitiveIndex,
            payload.barycentrics,
            GeomAttr_Normal | GeomAttr_TexCoord | GeomAttr_Position,
            t_InstanceData, t_GeometryData, t_MaterialConstants);
        
        MaterialSample ms = sampleGeometryMaterial(gs, 0, 0, 0,
            MatAttr_BaseColor | MatAttr_Emissive, s_MaterialSampler);

        if (g_Const.enableBrdfMIS)
        {
            if (isSpecularRay && any(ms.emissive > 0))
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

                radiance += ms.emissive * misWeight;
            }
        }
        else
        {
            radiance += ms.emissive;
        }

        if (g_Const.enableBrdfIndirect)
        {
            RAB_Surface secondarySurface;
            secondarySurface.worldPos = ray.Origin + ray.Direction * payload.committedRayT;
            secondarySurface.viewDepth = 1.0; // don't care
            secondarySurface.normal = (dot(gs.geometryNormal, ray.Direction) < 0) ? gs.geometryNormal : -gs.geometryNormal;
            secondarySurface.geoNormal = secondarySurface.normal;
            secondarySurface.baseColor = ms.baseColor;
            secondarySurface.metalness = 0.0; // use a simplified shading model for secondary hits
            secondarySurface.roughness = 1.0;

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
            if (lightSampleScale > 0)
            {
                float3 lightVisibility = GetFinalVisibility(secondarySurface, lightSample);

                float3 L = normalize(lightSample.position - secondarySurface.worldPos);

                indirectDiffuse = lightSample.radiance * lightSampleScale;
                indirectDiffuse *= lightVisibility;
                indirectDiffuse *= Lambert(secondarySurface.normal, -L);
                indirectDiffuse *= ms.baseColor;

                radiance += indirectDiffuse;
            }
        }
    }
    else if (g_Const.enableEnvironmentMap && (isSpecularRay || !g_Const.enableBrdfMIS) && BRDF_over_PDF > 0.0)
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
            u_SpecularLighting[lightingTexturePos] = RELAX_FrontEnd_PackRadiance(specular, specularHitT, surface.roughness);
        }
        else
        {
            float diffNormDist = REBLUR_FrontEnd_GetNormHitDist(diffuseHitT, surface.viewDepth, g_Const.reblurDiffHitDistParams);
            u_DiffuseLighting[lightingTexturePos] = REBLUR_FrontEnd_PackRadiance(diffuse, diffNormDist, surface.viewDepth, g_Const.reblurDiffHitDistParams);
            
            float specNormDist = REBLUR_FrontEnd_GetNormHitDist(specularHitT, surface.viewDepth, g_Const.reblurSpecHitDistParams, surface.roughness);
            u_SpecularLighting[lightingTexturePos] = REBLUR_FrontEnd_PackRadiance(specular, specNormDist, surface.viewDepth, g_Const.reblurSpecHitDistParams, surface.roughness);
        }
    }
    else
#endif
    {
        u_DiffuseLighting[lightingTexturePos] = float4(diffuse, diffuseHitT);
        u_SpecularLighting[lightingTexturePos] = float4(specular, specularHitT);
    }
}

/***************************************************************************
 # Copyright (c) 2021-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma pack_matrix(row_major)

#define SCENE_GEOMETRY_PIXEL_SHADER
#define ENABLE_METAL_ROUGH_RECONSTRUCTION 1

#include "ShaderParameters.h"
#include "SceneGeometry.hlsli"
#include "GBufferHelpers.hlsli"

struct InstanceConstants
{
    uint instance;
    uint geometryIndex;
};

ConstantBuffer<GBufferConstants> g_Const : register(b0);
VK_PUSH_CONSTANT ConstantBuffer<InstanceConstants> g_Instance : register(b1);

StructuredBuffer<InstanceData> t_InstanceData : register(t0);
StructuredBuffer<GeometryData> t_GeometryData : register(t1);
StructuredBuffer<MaterialConstants> t_MaterialConstants : register(t2);

SamplerState s_MaterialSampler : register(s0);

RWBuffer<uint> u_RayCountBuffer : register(u0);

/* The #ifdef SPIRV... parts in this shader are a workaround for DXC not having full
   support for SV_Barycentrics pixel shader inputs. It translates such inputs as BaryCoordSmoothAMD
   but NVIDIA drivers do not support that extension, and they need BaryCoordNV instead. */

void vs_main(
    in uint i_vertexID : SV_VertexID,
    out float4 o_position : SV_Position
#ifdef SPIRV
    ,
    out float3 o_objectPos : OBJECTPOS,
    out float3 o_prevObjectPos : PREV_OBJECTPOS,
    out float2 o_texcoord : TEXCOORD,
    out float3 o_normal : NORMAL,
    out float4 o_tangent : TANGENT
#endif
    )
{
    InstanceData instance = t_InstanceData[g_Instance.instance];
    GeometryData geometry = t_GeometryData[instance.firstGeometryIndex + g_Instance.geometryIndex];

    ByteAddressBuffer indexBuffer = t_BindlessBuffers[NonUniformResourceIndex(geometry.indexBufferIndex)];
    ByteAddressBuffer vertexBuffer = t_BindlessBuffers[NonUniformResourceIndex(geometry.vertexBufferIndex)];

    uint index = indexBuffer.Load(geometry.indexOffset + i_vertexID * 4);

    float3 objectSpacePosition = asfloat(vertexBuffer.Load3(geometry.positionOffset + index * c_SizeOfPosition));
    float3 prevObjectSpacePosition;

    float3 worldSpacePosition = mul(instance.transform, float4(objectSpacePosition, 1.0)).xyz;
    float4 clipSpacePosition = mul(float4(worldSpacePosition, 1.0), g_Const.view.matWorldToClip);

    o_position = clipSpacePosition;

#ifdef SPIRV
    o_objectPos = objectSpacePosition;
    if (geometry.prevPositionOffset != ~0u)
        o_prevObjectPos = asfloat(vertexBuffer.Load3(geometry.prevPositionOffset + index * c_SizeOfPosition));
    else
        o_prevObjectPos = o_objectPos;

    if (geometry.texCoord1Offset != ~0u)
        o_texcoord = asfloat(vertexBuffer.Load2(geometry.texCoord1Offset + index * c_SizeOfTexcoord));
    else
        o_texcoord = 0;

    if (geometry.normalOffset != ~0u)
    {
        o_normal = Unpack_RGB8_SNORM(vertexBuffer.Load(geometry.normalOffset + index * c_SizeOfNormal));
        o_normal = mul(instance.transform, float4(o_normal, 0.0)).xyz;
        o_normal = normalize(o_normal);
    }
    else
        o_normal = 0;

    if (geometry.tangentOffset != ~0u)
    {
        o_tangent = Unpack_RGBA8_SNORM(vertexBuffer.Load(geometry.tangentOffset + index * c_SizeOfNormal));
        o_tangent.xyz = mul(instance.transform, float4(o_tangent.xyz, 0.0)).xyz;
        o_tangent.xyz = normalize(o_tangent.xyz);
    }
    else
        o_tangent = 0;
#endif
}

#if !ALPHA_TESTED
[earlydepthstencil]
#endif
void ps_main(
    in float4 i_position : SV_Position,
    nointerpolation in uint i_primitiveID : SV_PrimitiveID,
#ifdef SPIRV
    in float3 i_objectPos : OBJECTPOS,
    in float3 i_prevObjectPos : PREV_OBJECTPOS,
    in float2 i_texcoord : TEXCOORD,
    in float3 i_normal : NORMAL,
    in float4 i_tangent : TANGENT,
#else
    in float3 i_bary : SV_Barycentrics,
#endif
    out float o_viewDepth : SV_Target0,
    out uint o_diffuseAlbedo : SV_Target1,
    out uint o_specularRough : SV_Target2,
    out uint o_normal : SV_Target3,
    out uint o_geoNormal : SV_Target4,
    out float4 o_emissive : SV_Target5,
    out float4 o_motion : SV_Target6
    )
{
#ifdef SPIRV
    GeometrySample gs = (GeometrySample)0;
    gs.instance = t_InstanceData[g_Instance.instance];
    gs.geometry = t_GeometryData[gs.instance.firstGeometryIndex + g_Instance.geometryIndex];
    gs.material = t_MaterialConstants[gs.geometry.materialIndex];

    gs.texcoord = i_texcoord;
    gs.objectSpacePosition = i_objectPos;
    gs.prevObjectSpacePosition = i_prevObjectPos;
    gs.geometryNormal = normalize(i_normal);
    gs.tangent.xyz = normalize(i_tangent.xyz);
    gs.tangent.w = i_tangent.w;
#else
    GeometrySample gs = getGeometryFromHit(g_Instance.instance, g_Instance.geometryIndex, i_primitiveID, i_bary.yz,
        GeomAttr_All, t_InstanceData, t_GeometryData, t_MaterialConstants);
#endif

    float3 worldSpacePosition = mul(gs.instance.transform, float4(gs.objectSpacePosition, 1.0)).xyz;
#ifdef SPIRV
    gs.flatNormal = normalize(cross(ddy(worldSpacePosition), ddx(worldSpacePosition)));
#endif
    
    float3 viewDirection = worldSpacePosition - g_Const.view.cameraDirectionOrPosition.xyz;
    float viewDistance = length(viewDirection);
    viewDirection /= viewDistance;

    if (dot(gs.geometryNormal, viewDirection) > 0)
        gs.geometryNormal = -gs.geometryNormal;

    MaterialSample ms = sampleGeometryMaterial(gs, g_Const.textureLodBias, MatAttr_All, s_MaterialSampler, g_Const.normalMapScale);
    
    ms.shadingNormal = getBentNormal(gs.flatNormal, ms.shadingNormal, viewDirection);

#if ALPHA_TESTED
    bool alphaMask = (ms.opacity >= gs.material.alphaCutoff);

    if (gs.material.domain == MaterialDomain_AlphaTested && !alphaMask)
        discard;
    else if (gs.material.domain == MaterialDomain_AlphaBlended)
        clip(ms.opacity - 0.5); // no support for blending
    else if (gs.material.domain == MaterialDomain_Transmissive ||
        (gs.material.domain == MaterialDomain_TransmissiveAlphaTested && alphaMask) ||
        gs.material.domain == MaterialDomain_TransmissiveAlphaBlended)
    {
        float throughput = ms.transmission;

        if ((gs.material.flags & MaterialFlags_UseSpecularGlossModel) == 0)
            throughput *= (1.0 - ms.metalness) * max(ms.baseColor.r, max(ms.baseColor.g, ms.baseColor.b));

        if (gs.material.domain == MaterialDomain_TransmissiveAlphaBlended)
            throughput *= (1.0 - ms.opacity);

        if (throughput != 0)
            discard;
    }
#endif

    if (all(g_Const.materialReadbackPosition == int2(i_position.xy)))
    {
        u_RayCountBuffer[g_Const.materialReadbackBufferIndex] = gs.geometry.materialIndex + 1;
    }

    if (g_Const.roughnessOverride >= 0)
        ms.roughness = g_Const.roughnessOverride;

    if (g_Const.metalnessOverride >= 0)
    {
        ms.metalness = g_Const.metalnessOverride;
        getReflectivity(ms.metalness, ms.baseColor, ms.diffuseAlbedo, ms.specularF0);
    }

    float clipDepth = 0;
    float viewDepth = 0;
    float3 motion = getMotionVector(g_Const.view, g_Const.viewPrev, 
        gs.instance, gs.objectSpacePosition, gs.prevObjectSpacePosition, clipDepth, viewDepth);

    o_viewDepth = viewDepth;
    o_diffuseAlbedo = Pack_R11G11B10_UFLOAT(ms.diffuseAlbedo);
    o_specularRough = Pack_R8G8B8A8_Gamma_UFLOAT(float4(ms.specularF0, ms.roughness));
    o_normal = ndirToOctUnorm32(ms.shadingNormal);
    o_geoNormal = ndirToOctUnorm32(gs.flatNormal);
    o_emissive = float4(ms.emissiveColor, viewDistance); // viewDistance is here to enable glass ray tracing on all pixels
    o_motion = float4(motion, 0);
}
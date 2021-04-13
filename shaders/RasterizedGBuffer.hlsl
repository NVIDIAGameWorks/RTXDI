/***************************************************************************
 # Copyright (c) 2021, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma pack_matrix(row_major)

#define SCENE_GEOMETRY_PIXEL_SHADER
#include "ShaderParameters.h"
#include "SceneGeometry.hlsli"
#include "GBufferHelpers.hlsli"

struct InstanceConstants
{
    uint instance;
};

ConstantBuffer<GBufferConstants> g_Const : register(b0);
VK_PUSH_CONSTANT ConstantBuffer<InstanceConstants> g_Instance : register(b1);

StructuredBuffer<InstanceData> t_InstanceData : register(t0);
StructuredBuffer<GeometryData> t_GeometryData : register(t1);
StructuredBuffer<MaterialConstants> t_MaterialConstants : register(t2);

SamplerState s_MaterialSampler : register(s0);

/* The #ifdef SPIRV... parts in this shader are a workaround for DXC not having full
   support for SV_Barycentrics pixel shader inputs. It translates such inputs as BaryCoordSmoothAMD
   but NVIDIA drivers do not support that extension, and they need BaryCoordNV instead. */

void vs_main(
    in uint i_vertexID : SV_VertexID,
    out float4 o_position : SV_Position
#ifdef SPIRV
    ,
    out float3 o_objectPos : OBJECTPOS,
    out float2 o_texcoord : TEXCOORD,
    out float3 o_normal : NORMAL,
    out float3 o_tangent : TANGENT,
    out float3 o_bitangent : BITANGENT
#endif
    )
{
    InstanceData instance = t_InstanceData[g_Instance.instance];
    GeometryData geometry = t_GeometryData[instance.geometryIndex];

    ByteAddressBuffer indexBuffer = t_BindlessBuffers[NonUniformResourceIndex(geometry.indexBufferIndex)];
    ByteAddressBuffer positionBuffer = t_BindlessBuffers[NonUniformResourceIndex(geometry.positionBufferIndex)];

    uint index = indexBuffer.Load(geometry.indexBufferOffset + i_vertexID * 4);

    float3 objectSpacePosition = asfloat(positionBuffer.Load3(geometry.positionBufferOffset + index * c_SizeOfPosition));

    float3 worldSpacePosition = mul(instance.transform, float4(objectSpacePosition, 1.0)).xyz;
    float4 clipSpacePosition = mul(float4(worldSpacePosition, 1.0), g_Const.view.matWorldToClip);

    o_position = clipSpacePosition;

#ifdef SPIRV
    ByteAddressBuffer texcoordBuffer = t_BindlessBuffers[NonUniformResourceIndex(geometry.texcoordBufferIndex)];
    ByteAddressBuffer normalBuffer = t_BindlessBuffers[NonUniformResourceIndex(geometry.normalBufferIndex)];
    ByteAddressBuffer tangentBuffer = t_BindlessBuffers[NonUniformResourceIndex(geometry.tangentBufferIndex)];
    ByteAddressBuffer bitangentBuffer = t_BindlessBuffers[NonUniformResourceIndex(geometry.bitangentBufferIndex)];
    
    o_objectPos = objectSpacePosition;

    o_texcoord = asfloat(texcoordBuffer.Load2(geometry.texcoordBufferOffset + index * c_SizeOfTexcoord));

    o_normal = Unpack_RGB8_SNORM(normalBuffer.Load(geometry.normalBufferOffset + index * c_SizeOfNormal));
    o_normal = mul(instance.transform, float4(o_normal, 0.0)).xyz;
    o_normal = normalize(o_normal);

    o_tangent = Unpack_RGB8_SNORM(tangentBuffer.Load(geometry.tangentBufferOffset + index * c_SizeOfNormal));
    o_tangent = mul(instance.transform, float4(o_tangent, 0.0)).xyz;
    o_tangent = normalize(o_tangent);

    o_bitangent = Unpack_RGB8_SNORM(bitangentBuffer.Load(geometry.bitangentBufferOffset + index * c_SizeOfNormal));
    o_bitangent = mul(instance.transform, float4(o_bitangent, 0.0)).xyz;
    o_bitangent = normalize(o_bitangent);
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
    in float2 i_texcoord : TEXCOORD,
    in float3 i_normal : NORMAL,
    in float3 i_tangent : TANGENT,
    in float3 i_bitangent : BITANGENT,
#else
    in float3 i_bary : SV_Barycentrics,
#endif
    out float o_viewDepth : SV_Target0,
    out uint o_baseColor : SV_Target1,
    out uint o_metalRough : SV_Target2,
    out uint o_normal : SV_Target3,
    out uint o_geoNormal : SV_Target4,
    out float4 o_emissive : SV_Target5,
    out float4 o_motion : SV_Target6,
    out float4 o_normalRough : SV_Target7
    )
{
#ifdef SPIRV
    GeometrySample gs = (GeometrySample)0;
    gs.instance = t_InstanceData[g_Instance.instance];
    gs.geometry = t_GeometryData[gs.instance.geometryIndex];
    gs.material = t_MaterialConstants[gs.geometry.materialIndex];

    gs.texcoord = i_texcoord;
    gs.objectSpacePosition = i_objectPos;
    gs.geometryNormal = normalize(i_normal);
    gs.tangent = normalize(i_tangent);
    gs.bitangent = normalize(i_bitangent);
#else
    GeometrySample gs = getGeometryFromHit(g_Instance.instance, i_primitiveID, i_bary.yz,
        GeomAttr_All, t_InstanceData, t_GeometryData, t_MaterialConstants);
#endif

    MaterialSample ms = sampleGeometryMaterial(gs, MatAttr_All, s_MaterialSampler, g_Const.normalMapScale);
    
#if ALPHA_TESTED
    int materialType = (gs.material.flags & MaterialFlags_MaterialType_Mask) >> MaterialFlags_MaterialType_Shift;
    if (materialType == MaterialType_Transparent || materialType == MaterialType_AlphaTested)
    {
        clip(ms.opacity - 0.5);
    }
#endif

    float3 worldSpacePosition = mul(gs.instance.transform, float4(gs.objectSpacePosition, 1.0)).xyz;
#ifdef SPIRV
    gs.flatNormal = normalize(cross(ddy(worldSpacePosition), ddx(worldSpacePosition)));
#endif
    float3 viewDirection = worldSpacePosition - g_Const.view.cameraDirectionOrPosition.xyz;
    float viewDistance = length(viewDirection);
    viewDirection /= viewDistance;
    ms.shadingNormal = getBentNormal(gs.flatNormal, ms.shadingNormal, viewDirection);

    if (g_Const.roughnessOverride >= 0)
        ms.roughness = g_Const.roughnessOverride;

    if (g_Const.metalnessOverride >= 0)
        ms.metalness = g_Const.metalnessOverride;

    float viewDepth = 0;
    float3 motion = getMotionVector(g_Const.view, g_Const.viewPrev, 
        gs.instance, gs.objectSpacePosition, viewDepth);

    o_viewDepth = viewDepth;
    o_baseColor = Pack_R8G8B8_UFLOAT(ms.baseColor);
    o_metalRough = Pack_R16G16_UFLOAT(float2(ms.metalness, ms.roughness));
    o_normal = ndirToOctUnorm32(ms.shadingNormal);
    o_geoNormal = ndirToOctUnorm32(gs.flatNormal);
    o_emissive = float4(ms.emissive, viewDistance); // viewDistance is here to enable glass ray tracing on all pixels
    o_motion = float4(motion, 0);
    o_normalRough = float4(ms.shadingNormal * 0.5 + 0.5, ms.roughness);
}
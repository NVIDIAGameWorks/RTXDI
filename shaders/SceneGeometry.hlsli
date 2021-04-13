/***************************************************************************
 # Copyright (c) 2020-2021, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#ifndef SCENE_GEOMETRY_HLSLI
#define SCENE_GEOMETRY_HLSLI

#include <donut/shaders/bindless.h>
#include <donut/shaders/vulkan.hlsli>
#include "HelperFunctions.hlsli"

VK_BINDING(0, 1) ByteAddressBuffer t_BindlessBuffers[] : register(t0, space1);
VK_BINDING(1, 1) Texture2D t_BindlessTextures[] : register(t0, space2);

enum GeometryAttributes
{
    GeomAttr_Position   = 0x01,
    GeomAttr_TexCoord   = 0x02,
    GeomAttr_Normal     = 0x04,
    GeomAttr_Tangents   = 0x08,

    GeomAttr_All        = 0x0F
};

struct GeometrySample
{
    InstanceData instance;
    GeometryData geometry;
    MaterialConstants material;

    float3 vertexPositions[3];
    float2 vertexTexcoords[3];

    float3 objectSpacePosition;
    float2 texcoord;
    float3 flatNormal;
    float3 geometryNormal;
    float3 tangent;
    float3 bitangent;
};

GeometrySample getGeometryFromHit(
    uint instanceIndex,
    uint triangleIndex,
    float2 rayBarycentrics,
    GeometryAttributes attributes,
    StructuredBuffer<InstanceData> instanceBuffer,
    StructuredBuffer<GeometryData> geometryBuffer,
    StructuredBuffer<MaterialConstants> materialBuffer)
{
    GeometrySample gs = (GeometrySample)0;

    gs.instance = instanceBuffer[instanceIndex];
    gs.geometry = geometryBuffer[gs.instance.geometryIndex];
    gs.material = materialBuffer[gs.geometry.materialIndex];

    ByteAddressBuffer indexBuffer = t_BindlessBuffers[NonUniformResourceIndex(gs.geometry.indexBufferIndex)];
    ByteAddressBuffer positionBuffer = t_BindlessBuffers[NonUniformResourceIndex(gs.geometry.positionBufferIndex)];
    ByteAddressBuffer texcoordBuffer = t_BindlessBuffers[NonUniformResourceIndex(gs.geometry.texcoordBufferIndex)];
    ByteAddressBuffer normalBuffer = t_BindlessBuffers[NonUniformResourceIndex(gs.geometry.normalBufferIndex)];
    ByteAddressBuffer tangentBuffer = t_BindlessBuffers[NonUniformResourceIndex(gs.geometry.tangentBufferIndex)];
    ByteAddressBuffer bitangentBuffer = t_BindlessBuffers[NonUniformResourceIndex(gs.geometry.bitangentBufferIndex)];

    float3 barycentrics;
    barycentrics.yz = rayBarycentrics;
    barycentrics.x = 1.0 - (barycentrics.y + barycentrics.z);

    uint3 indices = indexBuffer.Load3(gs.geometry.indexBufferOffset + triangleIndex * c_SizeOfTriangleIndices);

    if (attributes & GeomAttr_Position)
    {
        gs.vertexPositions[0] = asfloat(positionBuffer.Load3(gs.geometry.positionBufferOffset + indices[0] * c_SizeOfPosition));
        gs.vertexPositions[1] = asfloat(positionBuffer.Load3(gs.geometry.positionBufferOffset + indices[1] * c_SizeOfPosition));
        gs.vertexPositions[2] = asfloat(positionBuffer.Load3(gs.geometry.positionBufferOffset + indices[2] * c_SizeOfPosition));
        gs.objectSpacePosition = interpolate(gs.vertexPositions, barycentrics);
    }

    if (attributes & GeomAttr_TexCoord)
    {
        gs.vertexTexcoords[0] = asfloat(texcoordBuffer.Load2(gs.geometry.texcoordBufferOffset + indices[0] * c_SizeOfTexcoord));
        gs.vertexTexcoords[1] = asfloat(texcoordBuffer.Load2(gs.geometry.texcoordBufferOffset + indices[1] * c_SizeOfTexcoord));
        gs.vertexTexcoords[2] = asfloat(texcoordBuffer.Load2(gs.geometry.texcoordBufferOffset + indices[2] * c_SizeOfTexcoord));
        gs.texcoord = interpolate(gs.vertexTexcoords, barycentrics);
    }

    if (attributes & GeomAttr_Normal)
    {
        float3 normals[3];
        normals[0] = Unpack_RGB8_SNORM(normalBuffer.Load(gs.geometry.normalBufferOffset + indices[0] * c_SizeOfNormal));
        normals[1] = Unpack_RGB8_SNORM(normalBuffer.Load(gs.geometry.normalBufferOffset + indices[1] * c_SizeOfNormal));
        normals[2] = Unpack_RGB8_SNORM(normalBuffer.Load(gs.geometry.normalBufferOffset + indices[2] * c_SizeOfNormal));
        gs.geometryNormal = interpolate(normals, barycentrics);
        gs.geometryNormal = mul(gs.instance.transform, float4(gs.geometryNormal, 0.0)).xyz;
        gs.geometryNormal = normalize(gs.geometryNormal);
    }

    if (attributes & GeomAttr_Tangents)
    {
        float3 tangents[3];
        tangents[0] = Unpack_RGB8_SNORM(tangentBuffer.Load(gs.geometry.tangentBufferOffset + indices[0] * c_SizeOfNormal));
        tangents[1] = Unpack_RGB8_SNORM(tangentBuffer.Load(gs.geometry.tangentBufferOffset + indices[1] * c_SizeOfNormal));
        tangents[2] = Unpack_RGB8_SNORM(tangentBuffer.Load(gs.geometry.tangentBufferOffset + indices[2] * c_SizeOfNormal));
        gs.tangent = interpolate(tangents, barycentrics);
        gs.tangent = mul(gs.instance.transform, float4(gs.tangent, 0.0)).xyz;
        gs.tangent = normalize(gs.tangent);

        float3 bitangents[3];
        bitangents[0] = Unpack_RGB8_SNORM(bitangentBuffer.Load(gs.geometry.bitangentBufferOffset + indices[0] * c_SizeOfNormal));
        bitangents[1] = Unpack_RGB8_SNORM(bitangentBuffer.Load(gs.geometry.bitangentBufferOffset + indices[1] * c_SizeOfNormal));
        bitangents[2] = Unpack_RGB8_SNORM(bitangentBuffer.Load(gs.geometry.bitangentBufferOffset + indices[2] * c_SizeOfNormal));
        gs.bitangent = interpolate(bitangents, barycentrics);
        gs.bitangent = mul(gs.instance.transform, float4(gs.bitangent, 0.0)).xyz;
        gs.bitangent = normalize(gs.bitangent);
    }

    float3 objectSpaceFlatNormal = normalize(cross(
        gs.vertexPositions[1] - gs.vertexPositions[0],
        gs.vertexPositions[2] - gs.vertexPositions[0]));

    gs.flatNormal = normalize(mul(gs.instance.transform, float4(objectSpaceFlatNormal, 0.0)).xyz);

    return gs;
}

enum MaterialAttributes
{
    MatAttr_BaseColor   = 0x01,
    MatAttr_Emissive    = 0x02,
    MatAttr_Normal      = 0x04,
    MatAttr_MetalRough  = 0x08,

    MatAttr_All         = 0x0F
};

struct MaterialSample
{
    float3 baseColor;
    float3 emissive;
    float3 shadingNormal;
    float metalness;
    float roughness;
    float opacity;
};

MaterialSample sampleGeometryMaterial(
    GeometrySample gs, 
#ifndef SCENE_GEOMETRY_PIXEL_SHADER
    float2 texGrad_x, 
    float2 texGrad_y, 
    float mipLevel, // <-- Use a compile time constant for mipLevel, < 0 for aniso filtering
#endif
    MaterialAttributes attributes, 
    SamplerState materialSampler,
    float normalMapScale = 1.0)
{
    MaterialSample ms = (MaterialSample)0;

    float3 diffuse = gs.material.diffuseColor;
    float3 specular = gs.material.specularColor;
    ms.opacity = gs.material.opacity;

    if ((attributes & MatAttr_BaseColor) && (gs.material.diffuseTextureIndex >= 0))
    {
        Texture2D diffuseTexture = t_BindlessTextures[NonUniformResourceIndex(gs.material.diffuseTextureIndex)];

        float4 diffuseTextureValue;
#ifndef SCENE_GEOMETRY_PIXEL_SHADER
        if (mipLevel >= 0)
            diffuseTextureValue = diffuseTexture.SampleLevel(materialSampler, gs.texcoord, mipLevel);
        else
            diffuseTextureValue = diffuseTexture.SampleGrad(materialSampler, gs.texcoord, texGrad_x, texGrad_y);
#else
        diffuseTextureValue = diffuseTexture.Sample(materialSampler, gs.texcoord);
#endif
        
        diffuse *= diffuseTextureValue.rgb;
        ms.opacity *= diffuseTextureValue.a;
    }

    ms.emissive = gs.material.emissiveColor;

    if ((attributes & MatAttr_Emissive) && (gs.material.emissiveTextureIndex >= 0))
    {
        Texture2D emissiveTexture = t_BindlessTextures[NonUniformResourceIndex(gs.material.emissiveTextureIndex)];

        float4 emissiveTextureValue;
#ifndef SCENE_GEOMETRY_PIXEL_SHADER
        if (mipLevel >= 0)
            emissiveTextureValue = emissiveTexture.SampleLevel(materialSampler, gs.texcoord, mipLevel);
        else
            emissiveTextureValue = emissiveTexture.SampleGrad(materialSampler, gs.texcoord, texGrad_x, texGrad_y);
#else
        emissiveTextureValue = emissiveTexture.Sample(materialSampler, gs.texcoord);
#endif

        ms.emissive *= emissiveTextureValue.rgb;
    }

    ms.shadingNormal = gs.geometryNormal;

    if ((attributes & MatAttr_Normal) && (gs.material.normalsTextureIndex >= 0 && normalMapScale > 0))
    {
        Texture2D normalsTexture = t_BindlessTextures[NonUniformResourceIndex(gs.material.normalsTextureIndex)];

        float4 normalsTextureValue;
#ifndef SCENE_GEOMETRY_PIXEL_SHADER
        if (mipLevel >= 0)
            normalsTextureValue = normalsTexture.SampleLevel(materialSampler, gs.texcoord, mipLevel);
        else
            normalsTextureValue = normalsTexture.SampleGrad(materialSampler, gs.texcoord, texGrad_x, texGrad_y);
#else
        normalsTextureValue = normalsTexture.Sample(materialSampler, gs.texcoord);
#endif

        if(normalsTextureValue.z > 0)
        {
            float3 localNormal = normalize(normalsTextureValue.xyz * 2.0 - 1.0);
            localNormal.y = -localNormal.y;

            localNormal.xy *= normalMapScale;

            ms.shadingNormal = localNormal.x * gs.tangent + localNormal.y * gs.bitangent + localNormal.z * gs.geometryNormal;
            ms.shadingNormal = normalize(ms.shadingNormal);
        }
    }

    ms.metalness = 0;
    ms.roughness = gs.material.roughness;

    int specularType = ((gs.material.flags & MaterialFlags_SpecularType_Mask) >> MaterialFlags_SpecularType_Shift);
    
    if ((attributes & MatAttr_MetalRough) && (gs.material.specularTextureIndex >= 0))
    {
        Texture2D specularTexture = t_BindlessTextures[NonUniformResourceIndex(gs.material.specularTextureIndex)];

        float4 specularTextureValue;
#ifndef SCENE_GEOMETRY_PIXEL_SHADER
        if (mipLevel >= 0)
            specularTextureValue = specularTexture.SampleLevel(materialSampler, gs.texcoord, mipLevel);
        else
            specularTextureValue = specularTexture.SampleGrad(materialSampler, gs.texcoord, texGrad_x, texGrad_y);
#else
        specularTextureValue = specularTexture.Sample(materialSampler, gs.texcoord);
#endif
        
        if(specularType == SpecularType_MetalRough)
        {
            ms.roughness = 1 - (1 - ms.roughness) * (1 - specularTextureValue.g);

            ms.metalness = specularTextureValue.b;
        }
        else if(specularType == SpecularType_Color)
        {
            specular *= specularTextureValue.rgb;

            if (specularTextureValue.a > 0)
                ms.roughness = 1 - specularTextureValue.a * (1 - ms.roughness);
        }
        else
        {
            specular *= specularTextureValue.r;
        }
    }

    if(specularType == SpecularType_MetalRough || !(attributes & MatAttr_MetalRough))
    {
        ms.baseColor = diffuse;
    }
    else
    {
        // Roughly convert the diffuse and specular colors to the metal-rough model

        ms.baseColor = diffuse + specular;

        float baseMax = max(ms.baseColor.r, max(ms.baseColor.g, ms.baseColor.g));
        if(baseMax > 1.0)
            ms.baseColor /= baseMax;

        float diffuseLum = calcLuminance(diffuse);
        float specularLum = calcLuminance(specular);
        ms.metalness = saturate(specularLum / (diffuseLum + specularLum + 1e-8));
    }

    return ms;
}

#endif // SCENE_GEOMETRY_HLSLI
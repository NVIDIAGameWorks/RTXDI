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

#include "ShaderParameters.h"
#include "SceneGeometry.hlsli"
#include "GBufferHelpers.hlsli"

#ifdef WITH_NRD
#include <NRD.hlsl>
#endif

ConstantBuffer<CompositingConstants> g_Const : register(b0);

RWTexture2D<float4> u_Output : register(u0);
RWTexture2D<float4> u_MotionVectors : register(u1);

Texture2D<float> t_GBufferDepth : register(t0);
Texture2D<uint> t_GBufferNormals : register(t1);
Texture2D<uint> t_GBufferBaseColor : register(t2);
Texture2D<uint> t_GBufferMetalRough : register(t3);
Texture2D<float4> t_GBufferEmissive : register(t4);
Texture2D t_Diffuse : register(t5);
Texture2D t_Specular : register(t6);
Texture2D t_DenoisedDiffuse : register(t7);
Texture2D t_DenoisedSpecular : register(t8);

SamplerState s_EnvironmentSampler : register(s0);

[numthreads(8, 8, 1)]
void main(uint2 globalIdx : SV_DispatchThreadID)
{
    float3 compositedColor = 0;

    float depth = t_GBufferDepth[globalIdx];
    if (depth != 0)
    {
        float3 baseColor = Unpack_R8G8B8_UFLOAT(t_GBufferBaseColor[globalIdx]);
        float2 metal_rough = Unpack_R16G16_UFLOAT(t_GBufferMetalRough[globalIdx]);
        float3 emissive = t_GBufferEmissive[globalIdx].rgb;

        float4 diffuse_illumination;
        float4 specular_illumination;

#ifdef WITH_NRD
        if(g_Const.denoiserMode != DENOISER_MODE_OFF)
        {
            diffuse_illumination = t_DenoisedDiffuse[globalIdx].rgba;
            specular_illumination = t_DenoisedSpecular[globalIdx].rgba;
        }
        else
#endif
        {
            diffuse_illumination = t_Diffuse[globalIdx].rgba;
            specular_illumination = t_Specular[globalIdx].rgba;
        }

        float3 albedo = 1;
        float3 baseReflectivity = 1;
        if(g_Const.enableTextures)
        {
            getReflectivity(metal_rough.x, baseColor, albedo, baseReflectivity);
        }

        compositedColor = diffuse_illumination.rgb * albedo;
        compositedColor += specular_illumination.rgb * baseReflectivity;
        compositedColor += emissive.rgb;
    }
    else
    {   
        RayDesc primaryRay = setupPrimaryRay(globalIdx, g_Const.view);

        if (g_Const.enableEnvironmentMap)
        {
            Texture2D environmentLatLongMap = t_BindlessTextures[g_Const.environmentMapTextureIndex];
            float2 uv = directionToEquirectUV(primaryRay.Direction);
            uv.x -= g_Const.environmentRotation;
            compositedColor = environmentLatLongMap.SampleLevel(s_EnvironmentSampler, uv, 0).rgb;
            compositedColor *= g_Const.environmentScale;
        }

        float2 motionVector = getEnvironmentMotionVector(g_Const.view, g_Const.viewPrev, float2(globalIdx) + 0.5);
        u_MotionVectors[globalIdx] = float4(motionVector, 0, 0);
    }

    if(any(isnan(compositedColor)))
        compositedColor = float3(0, 0, 1);

    u_Output[globalIdx] = float4(compositedColor, 0);
}

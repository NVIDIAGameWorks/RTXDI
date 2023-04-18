/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
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
#define NRD_HEADER_ONLY
#include <NRDEncoding.hlsli>
#include <NRD.hlsli>
#endif

ConstantBuffer<CompositingConstants> g_Const : register(b0);

RWTexture2D<float4> u_Output : register(u0);
RWTexture2D<float4> u_MotionVectors : register(u1);

Texture2D<float> t_GBufferDepth : register(t0);
Texture2D<uint> t_GBufferNormals : register(t1);
Texture2D<uint> t_GBufferDiffuseAlbedo : register(t2);
Texture2D<uint> t_GBufferSpecularRough : register(t3);
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
    if (depth != BACKGROUND_DEPTH)
    {
        float3 normal = octToNdirUnorm32(t_GBufferNormals[globalIdx]);
        float3 diffuseAlbedo = Unpack_R11G11B10_UFLOAT(t_GBufferDiffuseAlbedo[globalIdx]);
        float3 specularF0 = Unpack_R8G8B8A8_Gamma_UFLOAT(t_GBufferSpecularRough[globalIdx]).rgb;
        float3 emissive = t_GBufferEmissive[globalIdx].rgb;

        int2 illuminationPos = globalIdx;
        if (g_Const.denoiserMode != DENOISER_MODE_OFF && g_Const.checkerboard)
        {
            // NRD takes the noisy input in checkerboard mode in one half of the screen.
            // Stretch that to full screen for correct noise mix-in behavior.
            illuminationPos.x /= 2;
        }

        float4 diffuse_illumination = t_Diffuse[illuminationPos].rgba;
        float4 specular_illumination = t_Specular[illuminationPos].rgba;

#ifdef WITH_NRD
        if(g_Const.denoiserMode != DENOISER_MODE_OFF)
        {
            float4 denoised_diffuse = t_DenoisedDiffuse[globalIdx].rgba;
            float4 denoised_specular = t_DenoisedSpecular[globalIdx].rgba;

            if (g_Const.denoiserMode == DENOISER_MODE_REBLUR)
            {
                denoised_diffuse = REBLUR_BackEnd_UnpackRadianceAndNormHitDist(denoised_diffuse);
                denoised_specular = REBLUR_BackEnd_UnpackRadianceAndNormHitDist(denoised_specular);
                
                diffuse_illumination = REBLUR_BackEnd_UnpackRadianceAndNormHitDist(diffuse_illumination);
                specular_illumination = REBLUR_BackEnd_UnpackRadianceAndNormHitDist(specular_illumination);
            }

            diffuse_illumination.rgb = lerp(denoised_diffuse.rgb,
                clamp(diffuse_illumination.rgb, denoised_diffuse.rgb * g_Const.noiseClampLow, denoised_diffuse.rgb * g_Const.noiseClampHigh),
                g_Const.noiseMix);

            specular_illumination.rgb = lerp(denoised_specular.rgb,
                clamp(specular_illumination.rgb, denoised_specular.rgb * g_Const.noiseClampLow, denoised_specular.rgb * g_Const.noiseClampHigh),
                g_Const.noiseMix);
        }
#endif

        if (g_Const.enableTextures)
        {
            diffuse_illumination.rgb *= diffuseAlbedo;
            specular_illumination.rgb *= max(0.01, specularF0);
        }

        compositedColor = diffuse_illumination.rgb;
        compositedColor += specular_illumination.rgb;
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

    u_Output[globalIdx] = float4(compositedColor, 1.0);
}

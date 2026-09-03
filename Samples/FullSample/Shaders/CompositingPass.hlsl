/*
 * SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#pragma pack_matrix(row_major)

#include "SharedShaderInclude/ShaderParameters.h"
#include "SceneGeometry.hlsli"
#include "GBufferHelpers.hlsli"

#ifdef WITH_NRD
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
// PSR material (packed R11G11B10); use when enableDenoiserPSR and pixel has PSR data
Texture2D<uint> t_PSRDiffuseAlbedo : register(t9);
Texture2D<uint> t_PSRSpecularF0 : register(t10);

SamplerState s_EnvironmentSampler : register(s0);

[numthreads(8, 8, 1)]
void main(uint2 globalIndex : SV_DispatchThreadID)
{
    float3 compositedColor = 0;

    float depth = t_GBufferDepth[globalIndex];
    if (depth != BACKGROUND_DEPTH)
    {
        float3 normal = octToNdirUnorm32(t_GBufferNormals[globalIndex]);
        float3 diffuseAlbedo = Unpack_R11G11B10_UFLOAT(t_GBufferDiffuseAlbedo[globalIndex]);
        float3 specularF0 = Unpack_R8G8B8A8_Gamma_UFLOAT(t_GBufferSpecularRough[globalIndex]).rgb;
        float3 emissive = t_GBufferEmissive[globalIndex].rgb;

        float3 PSRDiffuseAlbedo = Unpack_R11G11B10_UFLOAT(t_PSRDiffuseAlbedo[globalIndex]);
        float3 PSRSpecularF0 = Unpack_R11G11B10_UFLOAT(t_PSRSpecularF0[globalIndex]);
        // PSR surface
        if (any(PSRDiffuseAlbedo > 0.f) || any(PSRSpecularF0 > 0.f))
        {
            float metalness = GetMetalness(diffuseAlbedo, specularF0);
            diffuseAlbedo = lerp(diffuseAlbedo, PSRDiffuseAlbedo, metalness);
            specularF0 = lerp(specularF0, PSRSpecularF0, metalness);
        }

        int2 illuminationPos = globalIndex;
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
            float4 denoised_diffuse = t_DenoisedDiffuse[globalIndex].rgba;
            float4 denoised_specular = t_DenoisedSpecular[globalIndex].rgba;

            if (g_Const.denoiserMode == DENOISER_MODE_REBLUR)
            {
                denoised_diffuse = REBLUR_BackEnd_UnpackRadianceAndNormHitDist(denoised_diffuse);
                denoised_specular = REBLUR_BackEnd_UnpackRadianceAndNormHitDist(denoised_specular);

                diffuse_illumination = REBLUR_BackEnd_UnpackRadianceAndNormHitDist(diffuse_illumination);
                specular_illumination = REBLUR_BackEnd_UnpackRadianceAndNormHitDist(specular_illumination);
            }

            diffuse_illumination.rgb = denoised_diffuse.rgb;

            specular_illumination.rgb = denoised_specular.rgb;
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
        RayDesc primaryRay = SetupPrimaryRay(globalIndex, g_Const.view);

        if (g_Const.enableEnvironmentMap)
        {
            Texture2D environmentLatLongMap = t_BindlessTextures[g_Const.environmentMapTextureIndex];
            float2 uv = DirectionToEquirectUV(primaryRay.Direction);
            uv.x -= g_Const.environmentRotation;
            compositedColor = environmentLatLongMap.SampleLevel(s_EnvironmentSampler, uv, 0).rgb;
            compositedColor *= g_Const.environmentScale;
        }

        float2 motionVector = GetEnvironmentMotionVector(g_Const.view, g_Const.viewPrev, float2(globalIndex) + 0.5);
        u_MotionVectors[globalIndex] = float4(motionVector, 0, 0);
    }

    if(any(isnan(compositedColor)))
        compositedColor = float3(0, 0, 1);

    u_Output[globalIndex] = float4(compositedColor, 1.0);
}

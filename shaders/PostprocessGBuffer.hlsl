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

#include "ShaderParameters.h"

#include <donut/shaders/packing.hlsli>
#include <donut/shaders/utils.hlsli>

RWTexture2D<uint> u_SpecularRough : register(u0);
RWTexture2D<float4> u_NormalRoughness : register(u1);
Texture2D<uint> t_Normals : register(t0);
Texture2D<float> t_ViewDepth : register(t1);

#define NRD_BILATERAL_WEIGHT_VIEWZ_SENSITIVITY 100.0
#define NRD_BILATERAL_WEIGHT_CUTOFF            0.03

static const float kMirrorRoughness = 0.01f;

float GetBilateralWeight( float z, float zc )
{
    z = abs( z - zc ) * rcp( min( abs( z ), abs( zc ) ) + 0.001 ); \
    z = rcp( 1.0 + NRD_BILATERAL_WEIGHT_VIEWZ_SENSITIVITY * z ) * step( z, NRD_BILATERAL_WEIGHT_CUTOFF );
    return z;
}

float GetModifiedRoughnessFromNormalVariance( float linearRoughness, float3 nonNormalizedAverageNormal )
{
    // https://blog.selfshadow.com/publications/s2013-shading-course/rad/s2013_pbs_rad_notes.pdf (page 20)
    float l = length(nonNormalizedAverageNormal);
    float kappa = saturate(1.0 - l * l) * rcp(max(l * (3.0 - l * l), 1e-15));

    return sqrt(saturate(linearRoughness * linearRoughness + kappa));
}

[numthreads(16, 16, 1)]
void main(uint2 pixelPosition : SV_DispatchThreadID)
{
    float3 currentNormal = octToNdirUnorm32(t_Normals[pixelPosition]);
    float4 specularRough = Unpack_R8G8B8A8_Gamma_UFLOAT(u_SpecularRough[pixelPosition]);
    float currentLinearZ = t_ViewDepth[pixelPosition];

    float currentRoughness = specularRough.a;

    float3 averageNormal = currentNormal;
    float sumW = 1.0;

    for (int i = -1; i <= 1; i++)
    {
        for (int j = -1; j <= 1; j++)
        {
            // Skipping center pixel
            if ((i == 0) && (j == 0)) continue;

            int2 p = pixelPosition + int2(i, j);
            float3 pNormal = octToNdirUnorm32(t_Normals[p]);
            float pZ = t_ViewDepth[p];

            float w = GetBilateralWeight(currentLinearZ, pZ);

            averageNormal += pNormal * w;
            sumW += w;
        }
    }

    float invSumW = 1.0 / sumW;
    averageNormal *= invSumW;

    float currentRoughnessModified;
    if (currentRoughness <= kMirrorRoughness)
        currentRoughnessModified = 0;
    else 
        currentRoughnessModified = GetModifiedRoughnessFromNormalVariance(currentRoughness, averageNormal);

    u_SpecularRough[pixelPosition] = Pack_R8G8B8A8_Gamma_UFLOAT(float4(specularRough.rgb, currentRoughnessModified));
    u_NormalRoughness[pixelPosition] = float4(currentNormal * 0.5 + 0.5, currentRoughness);
}
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

#ifndef HELPER_FUNCTIONS_HLSLI
#define HELPER_FUNCTIONS_HLSLI

#include <donut/shaders/utils.hlsli>
#include <Rtxdi/Utils/Math.hlsli>

static const float c_pi = 3.1415926535;

// Maps ray hit UV into triangle barycentric coordinates
float3 HitUVToBarycentric(float2 hitUV)
{
    return float3(1 - hitUV.x - hitUV.y, hitUV.x, hitUV.y);
}

// Inverse of SampleTriangle
float2 RandomFromBarycentric(float3 barycentric)
{
    float sqrtx = 1 - barycentric.x;
    return float2(sqrtx * sqrtx, barycentric.z / sqrtx);
}

float CalcLuminance(float3 color)
{
    return dot(color.xyz, float3(0.299f, 0.587f, 0.114f));
}

/*https://graphics.pixar.com/library/OrthonormalB/paper.pdf*/
void BranchlessONB(in float3 n, out float3 b1, out float3 b2)
{
    float sign = n.z >= 0.0f ? 1.0f : -1.0f;
    float a = -1.0f / (sign + n.z);
    float b = n.x * n.y * a;
    b1 = float3(1.0f + sign * n.x * n.x * a, sign * b, -sign * n.x);
    b2 = float3(b, sign + n.y * n.y * a, -n.y);
}

float3 SphericalDirection(float sinTheta, float cosTheta, float sinPhi, float cosPhi, float3 x, float3 y, float3 z)
{
    return sinTheta * cosPhi * x + sinTheta * sinPhi * y + cosTheta * z;
}

void GetReflectivity(float metalness, float3 baseColor, out float3 o_albedo, out float3 o_baseReflectivity)
{
    const float dielectricSpecular = 0.04;
    o_albedo = lerp(baseColor * (1.0 - dielectricSpecular), 0, metalness);
    o_baseReflectivity = lerp(dielectricSpecular, baseColor, metalness);
}

// an approximate of the metalness based on diffuseAlbedo and specularF0
float GetMetalness(float3 diffuseAlbedo, float3 specularF0)
{
    // special case for perfect mirror
    if (all(diffuseAlbedo == 0.f)) return 1.f;

    float F0 = CalcLuminance(specularF0);
    float metalness = saturate(1.0417 * (F0 - 0.04)); // 1/0.96 = 1.0417

    return metalness;
}

float3 SampleGGX_VNDF(float3 Ve, float roughness, float2 random)
{
    float alpha = square(roughness);

    float3 Vh = normalize(float3(alpha * Ve.x, alpha * Ve.y, Ve.z));

    float lensq = square(Vh.x) + square(Vh.y);
    float3 T1 = lensq > 0.0 ? float3(-Vh.y, Vh.x, 0.0) / sqrt(lensq) : float3(1.0, 0.0, 0.0);
    float3 T2 = cross(Vh, T1);

    float r = sqrt(random.x);
    float phi = 2.0 * c_pi * random.y;
    float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    float s = 0.5 * (1.0 + Vh.z);
    t2 = (1.0 - s) * sqrt(1.0 - square(t1)) + s * t2;

    float3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - square(t1) - square(t2))) * Vh;

    // Tangent space H
    float3 Ne = float3(alpha * Nh.x, alpha * Nh.y, max(0.0, Nh.z));
    return Ne;
}

float2 DirectionToEquirectUV(float3 normalizedDirection)
{
    float elevation = asin(normalizedDirection.y);
    float azimuth = 0;
    if (abs(normalizedDirection.y) < 1.0)
        azimuth = atan2(normalizedDirection.z, normalizedDirection.x);

    float2 uv;
    uv.x = azimuth / (2 * c_pi) - 0.25;
    uv.y = 0.5 - elevation / c_pi;

    return uv;
}

float3 EquirectUVToDirection(float2 uv, out float cosElevation)
{
    float azimuth = (uv.x + 0.25) * (2 * c_pi);
    float elevation = (0.5 - uv.y) * c_pi;
    cosElevation = cos(elevation);

    return float3(
        cos(azimuth) * cosElevation,
        sin(elevation),
        sin(azimuth) * cosElevation
    );
}

#endif // HELPER_FUNCTIONS_HLSLI
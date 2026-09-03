/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#ifndef TRIANGLE_LIGHT_HLSLI
#define TRIANGLE_LIGHT_HLSLI

#include "HelperFunctions.hlsli"
#include <donut/shaders/brdf.hlsli>
#include <donut/shaders/packing.hlsli>

struct TriangleLight
{
    float3 base;
    float3 edge1;
    float3 edge2;
    float3 radiance;
    float3 normal;
    float surfaceArea;

    // Interface methods

    float CalcSolidAnglePdf(in const float3 viewerPosition,
                            in const float3 lightSamplePosition,
                            in const float3 lightSampleNormal)
    {
        float3 L = lightSamplePosition - viewerPosition;
        float Ldist = length(L);
        L /= Ldist;

        const float areaPdf = 1.0 / surfaceArea;
        const float sampleCosTheta = saturate(dot(L, -lightSampleNormal));

        return PdfAtoW(areaPdf, Ldist, sampleCosTheta);
    }

    // Helper methods

    static TriangleLight Create(in const RAB_LightInfo lightInfo)
    {
        TriangleLight triLight;

        triLight.edge1 = octToNdirUnorm32(lightInfo.direction1) * f16tof32(lightInfo.scalars);
        triLight.edge2 = octToNdirUnorm32(lightInfo.direction2) * f16tof32(lightInfo.scalars >> 16);
        triLight.base = lightInfo.center - (triLight.edge1 + triLight.edge2) / 3.0;
        triLight.radiance = Unpack_R16G16B16A16_FLOAT(lightInfo.radiance).rgb;

        float3 lightNormal = cross(triLight.edge1, triLight.edge2);
        float lightNormalLength = length(lightNormal);

        if(lightNormalLength > 0.0)
        {
            triLight.surfaceArea = 0.5 * lightNormalLength;
            triLight.normal = lightNormal / lightNormalLength;
        }
        else
        {
           triLight.surfaceArea = 0.0;
           triLight.normal = 0.0; 
        }

        return triLight;
    }

};

#endif // TRIANGLE_LIGHT_HLSLI

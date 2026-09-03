/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#ifndef RAB_MATERIAL_HLSLI
#define RAB_MATERIAL_HLSLI

#include "donut/shaders/brdf.hlsli"
#include "Rtxdi/Utils/RandomSamplerState.hlsli"

static const float kMinRoughness = 0.03f;

/*
* The RTXDI SDK RAB_Material implementation for the Full Sample uses a
* material model of two BRDFs:
* Diffuse is Lambertian
* Specular is GGX/Trowbridge-Reitz
*/

struct RAB_Material
{
    float3 diffuseAlbedo;
    float3 specularF0;
    float roughness;
	float3 emissiveColor;
};

RAB_Material RAB_EmptyMaterial()
{
    RAB_Material material = (RAB_Material)0;

    return material;
}

float3 GetDiffuseAlbedo(RAB_Material material)
{
    return material.diffuseAlbedo;
}

float3 GetSpecularF0(RAB_Material material)
{
    return material.specularF0;
}

float GetRoughness(RAB_Material material)
{
    return material.roughness;
}

float RAB_GetRoughness(RAB_Material material)
{
    return GetRoughness(material);
}

float3 RAB_GetEmissiveColor(RAB_Material material)
{
    return material.emissiveColor;
}

RAB_Material GetGBufferMaterial(
    int2 pixelPosition,
    PlanarViewConstants view,
    Texture2D<uint> diffuseAlbedoTexture, 
    Texture2D<uint> specularRoughTexture)
{
    RAB_Material material = RAB_EmptyMaterial();

    if (any(pixelPosition >= view.viewportSize))
        return material;

    material.diffuseAlbedo = Unpack_R11G11B10_UFLOAT(diffuseAlbedoTexture[pixelPosition]).rgb;
    float4 specularRough = Unpack_R8G8B8A8_Gamma_UFLOAT(specularRoughTexture[pixelPosition]);
    material.roughness = specularRough.a;
    material.specularF0 = specularRough.rgb;

    return material;
}

RAB_Material RAB_GetGBufferMaterial(
    int2 pixelPosition,
    bool prevFrame)
{
    if(prevFrame)
    {
        return GetGBufferMaterial(
            pixelPosition,
            g_Const.prevView,
            t_PrevGBufferDiffuseAlbedo,
            t_PrevGBufferSpecularRough);
    }
    else
    {
        return GetGBufferMaterial(
            pixelPosition,
            g_Const.view,
            t_GBufferDiffuseAlbedo,
            t_GBufferSpecularRough);
    }
}

// Compares the materials of two surfaces, returns true if the surfaces
// are similar enough that we can share the light reservoirs between them.
// If unsure, just return true.
bool RAB_AreMaterialsSimilar(RAB_Material a, RAB_Material b)
{
    const float roughnessThreshold = 0.5;
    const float reflectivityThreshold = 0.25;
    const float albedoThreshold = 0.25;

    if (!RTXDI_CompareRelativeDifference(a.roughness, b.roughness, roughnessThreshold))
        return false;

    if (abs(CalcLuminance(a.specularF0) - CalcLuminance(b.specularF0)) > reflectivityThreshold)
        return false;
    
    if (abs(CalcLuminance(a.diffuseAlbedo) - CalcLuminance(b.diffuseAlbedo)) > albedoThreshold)
        return false;

    return true;
}

#endif // RAB_MATERIAL_HLSLI
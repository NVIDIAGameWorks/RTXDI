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

ConstantBuffer<VisualizationConstants> g_Const : register(b0);
Texture2D<float> t_DiffuseConfidence : register(t0);
Texture2D<float> t_SpecularConfidence : register(t1);

// https://www.shadertoy.com/view/ls2Bz1
float3 ColorizeZucconi( float x )
{
    // Original solution converts visible wavelengths of light (400-700 nm) (represented as x = [0; 1]) to RGB colors
    x = saturate( x ) * 0.85;

    const float3 c1 = float3( 3.54585104, 2.93225262, 2.41593945 );
    const float3 x1 = float3( 0.69549072, 0.49228336, 0.27699880 );
    const float3 y1 = float3( 0.02312639, 0.15225084, 0.52607955 );

    float3 t = c1 * ( x - x1 );
    float3 a = saturate( 1.0 - t * t - y1 );

    const float3 c2 = float3( 3.90307140, 3.21182957, 3.96587128 );
    const float3 x2 = float3( 0.11748627, 0.86755042, 0.66077860 );
    const float3 y2 = float3( 0.84897130, 0.88445281, 0.73949448 );

    float3 k = c2 * ( x - x2 );
    float3 b = saturate( 1.0 - k * k - y2 );

    return saturate( a + b );
}


float4 main(float4 i_position : SV_Position) : SV_Target
{
    int2 pixelPos = int2(i_position.xy);

    int2 inputPos = int2(pixelPos * g_Const.resolutionScale);

    float input = 0;
    if (g_Const.visualizationMode == VIS_MODE_DIFFUSE_CONFIDENCE)
        input = t_DiffuseConfidence[inputPos];
    else if (g_Const.visualizationMode == VIS_MODE_SPECULAR_CONFIDENCE)
        input = t_SpecularConfidence[inputPos];
    input = saturate(1.0 - input);

    float4 result;

    result.a = pow(input, 0.5);
    result.rgb = ColorizeZucconi(input) * result.a;

    return result;
}

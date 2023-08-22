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
#include "HelperFunctions.hlsli"

#include <rtxdi/RtxdiParameters.h>
#include <rtxdi/RtxdiHelpers.hlsli>

ConstantBuffer<VisualizationConstants> g_Const : register(b0);
Texture2D<float4> t_CompositedColor : register(t0);
Texture2D<float4> t_ResolvedColor : register(t1);
Texture2D<float4> t_AccumulatedColor : register(t2);
Texture2D<float4> t_Diffuse : register(t3);
Texture2D<float4> t_Specular : register(t4);
Texture2D<float4> t_DenoisedDiffuse : register(t5);
Texture2D<float4> t_DenoisedSpecular : register(t6);
Texture2D<float4> t_Gradients : register(t7);
StructuredBuffer<RTXDI_PackedDIReservoir> t_Reservoirs : register(t8);
StructuredBuffer<RTXDI_PackedGIReservoir> t_GIReservoirs : register(t9);

#define RTXDI_LIGHT_RESERVOIR_BUFFER t_Reservoirs
#define RTXDI_GI_RESERVOIR_BUFFER t_GIReservoirs
#define RTXDI_ENABLE_STORE_RESERVOIR 0
#include <rtxdi/DIReservoir.hlsli>
#include <rtxdi/GIReservoir.hlsli>

float4 blend(float4 top, float4 bottom)
{
    return float4(top.rgb * top.a + bottom.rgb * (1.0 - top.a), 1.0 - (1.0 - top.a) * (1.0 - bottom.a));
}

float4 main(float4 i_position : SV_Position) : SV_Target
{
    int2 pixelPos = int2(i_position.xy);

    int2 viewportSize = g_Const.outputSize;
    int middle = viewportSize.y / 2;

    float2 resolutionScale = g_Const.resolutionScale;
    if (g_Const.visualizationMode == VIS_MODE_RESOLVED_COLOR)
        resolutionScale = 1.0;

    int2 inputPos = int2(pixelPos.x * resolutionScale.x, g_Const.outputSize.y * resolutionScale.y * 0.5);
    int2 reservoirPos = RTXDI_PixelPosToReservoirPos(inputPos, g_Const.runtimeParams.activeCheckerboardField);
    float input = 0;

    switch(g_Const.visualizationMode)
    {
    case VIS_MODE_COMPOSITED_COLOR:
        input = calcLuminance(t_CompositedColor[inputPos].rgb);
        break;

    case VIS_MODE_RESOLVED_COLOR:
        if (g_Const.enableAccumulation)
            input = calcLuminance(t_AccumulatedColor[inputPos].rgb);
        else
            input = calcLuminance(t_ResolvedColor[inputPos].rgb);
        break;

    case VIS_MODE_DIFFUSE:
        input = calcLuminance(t_Diffuse[inputPos].rgb);
        break;

    case VIS_MODE_SPECULAR:
        input = calcLuminance(t_Specular[inputPos].rgb);
        break;
        
    case VIS_MODE_DENOISED_DIFFUSE:
        input = calcLuminance(t_DenoisedDiffuse[inputPos].rgb);
        break;
        
    case VIS_MODE_DENOISED_SPECULAR:
        input = calcLuminance(t_DenoisedSpecular[inputPos].rgb);
        break;
        
    case VIS_MODE_RESERVOIR_WEIGHT: {
        RTXDI_DIReservoir reservoir = RTXDI_LoadDIReservoir(g_Const.restirDIReservoirBufferParams, reservoirPos, g_Const.inputBufferIndex);
        input = reservoir.weightSum;
        break;
    }

    case VIS_MODE_RESERVOIR_M: {
        RTXDI_DIReservoir reservoir = RTXDI_LoadDIReservoir(g_Const.restirDIReservoirBufferParams, reservoirPos, g_Const.inputBufferIndex);
        input = reservoir.M;
        break;
    }

    case VIS_MODE_DIFFUSE_GRADIENT: {
        float4 gradient = t_Gradients[reservoirPos / RTXDI_GRAD_FACTOR];
        input = gradient.x;
        break;
    }

    case VIS_MODE_SPECULAR_GRADIENT: {
        float4 gradient = t_Gradients[reservoirPos / RTXDI_GRAD_FACTOR];
        input = gradient.y;
        break;
    }
                                   
    case VIS_MODE_GI_WEIGHT: {
        RTXDI_GIReservoir reservoir = RTXDI_LoadGIReservoir(g_Const.restirGIReservoirBufferParams, reservoirPos, g_Const.inputBufferIndex);
        input = reservoir.weightSum;
        break;
    }

    case VIS_MODE_GI_M: {
        RTXDI_GIReservoir reservoir = RTXDI_LoadGIReservoir(g_Const.restirGIReservoirBufferParams, reservoirPos, g_Const.inputBufferIndex);
        input = reservoir.M;
        break;
    }
    }

    float logLum = log2(input) / log2(10.0);
    int pos = middle - int(logLum * 100);
    
    float4 result = 0;

    int linePos = (middle - pixelPos.y + 1000) % 100;
    if (middle == pixelPos.y)
        result = blend(float4(1, 1, 0, 0.5), result);
    else if (linePos == 0)
        result = blend(float4(1, 1, 0, 0.2), result);
    else if (linePos == 30 || linePos == 48 || linePos == 60 || linePos == 69 || linePos == 78 || linePos == 84 || linePos == 90 || linePos == 95)
        result = blend(float4(1, 0, 0, 0.1), result);

    float height = 30;

    if (isinf(logLum))
    {
        float alpha = square(max(float(pixelPos.y - viewportSize.y) + height, 0) / height);
        result = blend(float4(1, 1, 0, alpha), result);
    }
    else if (i_position.y >= pos)
    {
        float alpha = square(max(float(pos - pixelPos.y) + height, 0) / height);
        result = blend(float4(0, 1, 1, alpha), result);
    }

    return result;
}

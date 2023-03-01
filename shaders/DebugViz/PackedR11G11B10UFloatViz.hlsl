/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#ifndef PACKED_NORMAL_HLSL
#define PACKED_NORMAL_HLSL

#include "../ShaderParameters.h"

#include <donut/shaders/packing.hlsli>

Texture2D<uint> t_PackedVecs : register(t0);
RWTexture2D<float4> t_Output : register(u0);

[numthreads(16, 16, 1)]
void main(uint2 pixelPosition : SV_DispatchThreadID)
{
    uint packedVec = t_PackedVecs[pixelPosition];
    float3 unpackedVec = Unpack_R11G11B10_UFLOAT(packedVec);
    t_Output[pixelPosition] = float4(unpackedVec, 1.0);
}

#endif // PACKED_NORMAL_HLSL
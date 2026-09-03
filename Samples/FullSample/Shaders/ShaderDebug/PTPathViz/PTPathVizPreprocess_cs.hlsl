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

#ifndef PT_PATH_VIZ_PREPROCESS_CS_HLSL
#define PT_PATH_VIZ_PREPROCESS_CS_HLSL

#include "SharedShaderInclude/ShaderDebug/PTPathViz/PTPathVizConstants.h"
#include "SharedShaderInclude/ShaderDebug/PTPathViz/PTPathVertexRecord.h"
#include "SharedShaderInclude/ShaderDebug/PTPathViz/PTPathSetRecord.h"

ConstantBuffer<PTPathVizPreprocessConstants> g_preprocessCB : register(b0);

StructuredBuffer<PTPathVertexRecord> s_pathRecord : register(t0);
StructuredBuffer<PTPathSet> g_pathSetRecord : register(t1);

RWBuffer<float4> g_pathVertexLineList : register(u0);
RWBuffer<float4> g_pathNormalLineList : register(u1);
RWBuffer<float4> g_pathNEELineList : register(u2);

#define CS_THREADGROUP_WIDTH 32
#define CS_THREADGROUP_HEIGHT 1

uint PackPathTypeAndLineType(RTXDI_PTPathTraceInvocationType pathType, DEBUG_PATH_VIZ_LINE_TYPE lineType)
{
    return ((uint)pathType << 2) | (lineType & 0x3);
}

void NullifyOutput(uint oIdx1, uint oIdx2)
{
    g_pathVertexLineList[oIdx1] = float4(0.0f, 0.0f, 0.0f, 0.0f);
    g_pathVertexLineList[oIdx2] = float4(0.0f, 0.0f, 0.0f, 0.0f);

    g_pathNormalLineList[oIdx1] = float4(0.0f, 0.0f, 0.0f, 0.0f);
    g_pathNormalLineList[oIdx2] = float4(0.0f, 0.0f, 0.0f, 0.0f);

    g_pathNEELineList[oIdx1] = float4(0.0f, 0.0f, 0.0f, 0.0f);
    g_pathNEELineList[oIdx2] = float4(0.0f, 0.0f, 0.0f, 0.0f);
}

[numthreads(CS_THREADGROUP_WIDTH, CS_THREADGROUP_HEIGHT, 1)]
void main(uint3 globalIndex : SV_DispatchThreadID)
{
    uint totalVertexCount = g_pathSetRecord[0].vertexBeginIndex;
    uint totalPathCount = g_pathSetRecord[0].vertexEndIndex;

    uint localVertexIndex = globalIndex.x;
    uint currentPathIndex = globalIndex.y;

    // Skip special first entry when addressing into output buffers
    uint oIdx1 = 2 * (g_preprocessCB.maxVerticesPerPath * (currentPathIndex-1) + localVertexIndex);
    uint oIdx2 = 2 * (g_preprocessCB.maxVerticesPerPath * (currentPathIndex-1) + localVertexIndex) + 1;

    float4 v1 = 0;
    float4 v2 = 0;
    float4 n1 = 0;
    float4 n2 = 0;
    float4 l1 = 0;
    float4 l2 = 0;

	if(currentPathIndex <= totalPathCount && (currentPathIndex != 0))
	{
        uint globalVertexStartIndex = g_pathSetRecord[currentPathIndex].vertexBeginIndex;
        uint globalVertexEndIndex = g_pathSetRecord[currentPathIndex].vertexEndIndex;
        uint verticesInCurrentPath = globalVertexEndIndex - globalVertexStartIndex;

        RTXDI_PTPathTraceInvocationType type = g_pathSetRecord[currentPathIndex].type;

        // Don't draw degenerate paths && Last vertex is endpoint of last line
        if (verticesInCurrentPath > 0 && (localVertexIndex <= verticesInCurrentPath))
        {
            uint globalVertexIndex = localVertexIndex + globalVertexStartIndex;
            uint packedType = PackPathTypeAndLineType(type, DEBUG_PATH_VIZ_LINE_TYPE::PATH);

            if (localVertexIndex < verticesInCurrentPath)
            {
                v1 = float4(s_pathRecord[globalVertexIndex].position, packedType);
                v2 = float4(s_pathRecord[globalVertexIndex + 1].position, packedType);
            }

            // The camera vertex has no normal vector.
            if (localVertexIndex > 0)
            {
                uint packedType = PackPathTypeAndLineType(type, DEBUG_PATH_VIZ_LINE_TYPE::NORMAL);
                n1 = float4(s_pathRecord[globalVertexIndex].position, packedType);
                n2 = float4(s_pathRecord[globalVertexIndex].position + (g_preprocessCB.normalVectorVizLength * s_pathRecord[globalVertexIndex].normal), packedType);
            }
            // Only draw NEE light rays for secondary surfaces and beyond
            if (localVertexIndex > 1)
            {
                uint packedType = PackPathTypeAndLineType(type, DEBUG_PATH_VIZ_LINE_TYPE::NEE);
                l1 = float4(s_pathRecord[globalVertexIndex].position, packedType);
                l2 = float4(s_pathRecord[globalVertexIndex].neeLightPosition, packedType);
            }
        }
	}

    g_pathVertexLineList[oIdx1] = v1;
    g_pathVertexLineList[oIdx2] = v2;

    g_pathNormalLineList[oIdx1] = n1;
    g_pathNormalLineList[oIdx2] = n2;

    g_pathNEELineList[oIdx1] = l1;
    g_pathNEELineList[oIdx2] = l2;
}

#endif // PT_PATH_VIZ_PREPROCESS_CS_HLSL
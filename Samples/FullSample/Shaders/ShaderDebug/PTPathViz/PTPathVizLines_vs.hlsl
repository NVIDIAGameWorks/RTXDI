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

#ifndef PT_PATH_VIZ_LINES_VS_HLSL
#define PT_PATH_VIZ_LINES_VS_HLSL

#include "SharedShaderInclude/ShaderDebug/PTPathViz/PTPathVizConstants.h"
#include "SharedShaderInclude/ShaderDebug/PTPathViz/PTPathSetRecord.h"
#include "Rtxdi/PT/ReSTIRPTParameters.h"

ConstantBuffer<PTPathVizLineConstants> g_Const : register(b0);

bool TypeIsEnabled(RTXDI_PTPathTraceInvocationType pathType, DEBUG_PATH_VIZ_LINE_TYPE lineType)
{
    if (lineType == NORMAL)
    {
        if (!g_Const.paths.normal.enabled)
            return false;
    }
    if (lineType == NEE)
    {
        if (!g_Const.paths.nee.enabled)
            return false;
    }

    switch (pathType)
    {
    case RTXDI_PTPathTraceInvocationType_Initial:
        return g_Const.paths.initial.enabled;
    case RTXDI_PTPathTraceInvocationType_DebugTemporalRetrace:
        return g_Const.paths.temporalRetrace.enabled;
    case RTXDI_PTPathTraceInvocationType_Temporal:
        return g_Const.paths.temporalShift.enabled;
    case RTXDI_PTPathTraceInvocationType_TemporalInverse:
        return g_Const.paths.temporalInverseShift.enabled;
    case RTXDI_PTPathTraceInvocationType_DebugSpatialRetrace:
        return g_Const.paths.spatialRetrace.enabled;
    case RTXDI_PTPathTraceInvocationType_Spatial:
        return g_Const.paths.spatialShift.enabled;
    case RTXDI_PTPathTraceInvocationType_SpatialInverse:
        return g_Const.paths.spatialInverseShift.enabled;
    default:
        return false;
    }
}

float3 TypeToColor(RTXDI_PTPathTraceInvocationType pathType, DEBUG_PATH_VIZ_LINE_TYPE lineType)
{
    if (lineType == NORMAL)
        return g_Const.paths.normal.color;
    if (lineType == NEE)
        return g_Const.paths.nee.color;

    switch (pathType)
    {
    case RTXDI_PTPathTraceInvocationType_Initial:
        return g_Const.paths.initial.color;
    case RTXDI_PTPathTraceInvocationType_DebugTemporalRetrace:
        return g_Const.paths.temporalRetrace.color;
    case RTXDI_PTPathTraceInvocationType_Temporal:
        return g_Const.paths.temporalShift.color;
    case RTXDI_PTPathTraceInvocationType_TemporalInverse:
        return g_Const.paths.temporalInverseShift.color;
    case RTXDI_PTPathTraceInvocationType_DebugSpatialRetrace:
        return g_Const.paths.spatialRetrace.color;
    case RTXDI_PTPathTraceInvocationType_Spatial:
        return g_Const.paths.spatialShift.color;
    case RTXDI_PTPathTraceInvocationType_SpatialInverse:
        return g_Const.paths.spatialInverseShift.color;
    default:
        return float3(0.0f, 0.0f, 0.0f);
    }
}

void UnpackPathTypeAndLineType(in const uint packedData,
    out RTXDI_PTPathTraceInvocationType pathType,
    out DEBUG_PATH_VIZ_LINE_TYPE lineType)
{
    pathType = (RTXDI_PTPathTraceInvocationType)(packedData >> 2);
    lineType = (DEBUG_PATH_VIZ_LINE_TYPE)(packedData & 3);
}

bool IsOnCameraToPrimaryPath(uint vId)
{
    uint localVid = vId % g_Const.maxVerticesPerPath;
    return localVid == 0 || localVid == 1;
}

void vs_main(
	in float4 i_pos : POSITION,
    in uint vId : SV_VertexID,
	out float4 o_posClip : SV_Position,
    out float3 o_color : COLOR0)
{
    uint packedType = (uint)i_pos.w;
    RTXDI_PTPathTraceInvocationType pathType = RTXDI_PTPathTraceInvocationType_Initial;
    DEBUG_PATH_VIZ_LINE_TYPE lineType = PATH;
    UnpackPathTypeAndLineType(packedType, pathType, lineType);

    o_color = TypeToColor(pathType, lineType);

    bool enabled = TypeIsEnabled(pathType, lineType);
    if (!g_Const.enableCameraVertex && IsOnCameraToPrimaryPath(vId))
    {
        enabled = false;
    }

    float4 pos = enabled ? float4(i_pos.xyz, 1.0) : float4(0.0, 0.0, 0.0, 0.0);
	o_posClip = mul(g_Const.view.matWorldToClip, pos);
}


#endif // PT_PATH_VIZ_LINES_VS_HLSL
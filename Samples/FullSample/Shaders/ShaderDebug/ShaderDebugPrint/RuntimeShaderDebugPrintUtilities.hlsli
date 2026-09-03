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

#ifndef RUNTIME_SHADER_DEBUG_PRINT_UTILITIES_HLSLI
#define RUNTIME_SHADER_DEBUG_PRINT_UTILITIES_HLSLI

void PrintPathType(RTXDI_PTPathTraceInvocationType type)
{
    switch(type)
    {
    case RTXDI_PTPathTraceInvocationType_Initial:
        DebugPrint_("Initial");
        break;
    case RTXDI_PTPathTraceInvocationType_Temporal:
        DebugPrint_("Temporal");
        break;
    case RTXDI_PTPathTraceInvocationType_TemporalInverse:
        DebugPrint_("Temporal Inverse");
        break;
    case RTXDI_PTPathTraceInvocationType_Spatial:
        DebugPrint_("Spatial");
        break;
    case RTXDI_PTPathTraceInvocationType_SpatialInverse:
        DebugPrint_("Spatial Inverse");
        break;
    case RTXDI_PTPathTraceInvocationType_DebugTemporalRetrace:
        DebugPrint_("Temporal Retrace");
        break;
    case RTXDI_PTPathTraceInvocationType_DebugSpatialRetrace:
        DebugPrint_("Spatial Retrace");
        break;
    }
}

void PrintReservoir_(RTXDI_PTReservoir r)
{
    DebugPrint_("Reservoir:");
    DebugPrint_("- TWP: {0}", r.translatedWorldPosition);
    DebugPrint_("- WS: {0}", r.weightSum);
    DebugPrint_("- WN: {0}", r.worldNormal);
    DebugPrint_("- M: {0}", r.M);
    DebugPrint_("- R: {0}", r.radiance);
    DebugPrint_("- Age: {0}", r.age);
    DebugPrint_("- AuxFlag: {0}", r.auxFlag);
    DebugPrint_("- RcWiPdf: {0}", r.rcWiPdf);
    DebugPrint_("- PJ: {0}", r.partialJacobian);
    DebugPrint_("- RcVL: {0}", r.rcVertexLength);
    DebugPrint_("- PL: {0}", r.pathLength);
    DebugPrint_("- RS: {0}", r.randomSeed);
    DebugPrint_("- RI: {0}", r.randomIndex);
    DebugPrint_("- TF: {0}", r.targetFunction);
}

#endif // RUNTIME_SHADER_DEBUG_PRINT_UTILITIES_HLSLI

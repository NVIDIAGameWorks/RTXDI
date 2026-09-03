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

#ifndef RAB_DENOISER_CALLBACK_HLSLI
#define RAB_DENOISER_CALLBACK_HLSLI

#include "../RAB_Surface.hlsli"

#include "Rtxdi/PT/Reservoir.hlsli"

/*
 * Because RAB_PathTracer is a fully user-defined function,
 * it can keep track of nearly all of the information required
 * for the denoiser.
 * However, there are two cases where the RTXDI_ComputeHybridShift
 * function contains information about the path that the path
 * tracer does not - after random replay to the vertex before the
 * reconnection vertex, and after random replay to the last vertex
 * before a light.
 * The two functions here are called in each instance to allow for
 * the path tracer user data to calculate denoising information as
 * needed.
 */

/*
 * This callback occurs during hybrid shift (in both temporal and
 * spatial resampling) after the path tracer re-traces the path
 * to the vertex before the reconnection vertex.
 *
 * This callback function allows the intgration target code to
 * calculate denoiser values as needed.
 *
 * neighborSample: Neighbor reservoir that is being resampled. It
                     holds the world position of the reconnection
                     vertex.
 * rcPrevSurface: Surface before the reconnection vertex in the
 *                  neighborSample.
 * ptud: User-defined output struct
 */
void RAB_ReconnectionDenoiserCallback(const RTXDI_PTReservoir neighborSample,
                                      const RAB_Surface rcPrevSurface,
                                      inout RAB_PathTracerUserData ptud)
{
    float3 L = normalize(neighborSample.translatedWorldPosition - rcPrevSurface.worldPos);
    if (ptud.psr.active)
    {
        if (ptud.psr.hasRecorded())
            ptud.psr.packedL = ndirToOctUnorm32(mul(L, ptud.psr.mirrorMatrix));
        if (ptud.psr.hitT == 0.f)
            ptud.psr.hitT = length(neighborSample.translatedWorldPosition - rcPrevSurface.worldPos);
    }
}

/*
 * This callback occurs during hybrid shift (in both temporal and
 * spatial resampling) after the path tracer re-traces the path
 * to the last vertex before an NEE-sampled light.
 *
 * This callback function allows the intgration target code to
 * calculate denoiser values as needed.
 * 
 * lightPos: Position of the light to which the surface is bouncing
 * preLightSurface: Surface whose bounce is going to lightPos
 * ptud: User-defined output struct
 */

void RAB_LastBounceDenoiserCallback(const float3 lightPos,
                                    const RAB_Surface preLightSurface,
                                    inout RAB_PathTracerUserData ptud)
{
    float3 lightDir = normalize(lightPos - RAB_GetSurfaceWorldPos(preLightSurface));
    if (ptud.psr.active)
    {
        if (ptud.psr.hasRecorded())
            ptud.psr.packedL = ndirToOctUnorm32(mul(lightDir, ptud.psr.mirrorMatrix));
        if (ptud.psr.hitT == 0.f)
            ptud.psr.hitT = length(lightPos - RAB_GetSurfaceWorldPos(preLightSurface));
    }

    Debug_RecordPTLastNEELightPositionForPriorPath(lightPos);
}

#endif // RAB_DENOISER_CALLBACK_HLSLI

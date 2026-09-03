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

#pragma pack_matrix(row_major)

#include "../RtxdiApplicationBridge/RtxdiApplicationBridge.hlsli"
#include "../../ShaderDebug/ShaderDebugPrint/ShaderDebugPrint.hlsli"
#include <Rtxdi/PT/Reservoir.hlsli>
#include <Rtxdi/PT/Decorrelation.hlsli>
#include <Rtxdi/Utils/RandomSamplerPerPassSeeds.hlsli>
#include <Rtxdi/Utils/ReservoirAddressing.hlsli>

#ifdef WITH_NRD
#define NRD_HEADER_ONLY
#include <NRD.hlsli>
#endif

#include "../ShadingHelpers.hlsli"

bool InvalidPixelPosition(int2 pixelPosition)
{
	return any(pixelPosition > int2(g_Const.view.viewportSize));
}

void InvalidateReservoirBuffer(RTXDI_ReservoirBufferParameters bufferParams, uint2 reservoirPosition, uint reservoirArrayIndex)
{
    RTXDI_PTReservoir emptyReservoir = RTXDI_EmptyPTReservoir();
	RTXDI_StorePTReservoir(emptyReservoir, bufferParams, reservoirPosition, reservoirArrayIndex);
}

float3 InvalidPixelGuard(float3 value)
{
	value = select(isnan(value), float3(0.f, 0.f, 0.f), value);
	value = select(value < 0.f, float3(0.f, 0.f, 0.f), value);
	value = select(isinf(value), float3(0.f, 0.f, 0.f), value);

	return value;
}

SplitBrdf CalculateDirectLighting(RAB_Surface primarySurface, RTXDI_PTReservoir ptReservoir)
{
	const float3 N = RAB_GetSurfaceNormal(primarySurface);
	const float3 V = RAB_GetSurfaceViewDir(primarySurface);

	float3 L = normalize(ptReservoir.translatedWorldPosition - RAB_GetSurfaceWorldPos(primarySurface));

	if (ptReservoir.rcVertexLength > 2)
	{
        RTXDI_RandomSamplerState randomReplayRng = RTXDI_GetRngForShading(ptReservoir);
        RTXDI_BrdfRaySampleProperties bsrp = RTXDI_DefaultBrdfRaySampleProperties();
        RAB_SurfaceImportanceSampleBsdf(primarySurface, randomReplayRng, L, bsrp);
	}

	return EvaluateBrdfPT(primarySurface, N, V, L);
}

void CalculateDiffuseAndSpecular(SplitBrdf lightingSample, RAB_Surface primarySurface, RTXDI_PTReservoir ptReservoir, inout float3 diffuse, inout float3 specular)
{
    float3 pHatUCW = ptReservoir.weightSum * ptReservoir.targetFunction;
    float3 totalBrdf = lightingSample.demodulatedDiffuse * primarySurface.material.diffuseAlbedo + lightingSample.specular;
    diffuse = lightingSample.demodulatedDiffuse * pHatUCW / totalBrdf; // demodulated
    specular = lightingSample.specular * pHatUCW / totalBrdf;
    diffuse = InvalidPixelGuard(diffuse);
    specular = InvalidPixelGuard(specular);
}

#if USE_RAY_QUERY
[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 globalIndex: SV_DispatchThreadID)
#else
[shader("raygeneration")]
void RayGen()
#endif
{
#if !USE_RAY_QUERY
    uint2 globalIndex = DispatchRaysIndex().xy;
#endif

    uint2 pixelPosition = RTXDI_ReservoirPosToPixelPos(globalIndex, g_Const.runtimeParams.activeCheckerboardField);
	const uint2 reservoirPosition = RTXDI_PixelPosToReservoirPos(pixelPosition, g_Const.runtimeParams.activeCheckerboardField);

    if (InvalidPixelPosition(pixelPosition))
	{
		InvalidateReservoirBuffer(g_Const.restirPT.reservoirBuffer, reservoirPosition, g_Const.restirPT.bufferIndices.finalShadingInputBufferIndex);
        return;
	}

	RAB_Surface primarySurface = RAB_GetGBufferSurface(pixelPosition, false);
	if (!RAB_IsSurfaceValid(primarySurface))
	{
		InvalidateReservoirBuffer(g_Const.restirPT.reservoirBuffer, reservoirPosition, g_Const.restirPT.bufferIndices.finalShadingInputBufferIndex);
        if (g_Const.debug.outputDebugIndirectLighting)
        {
            u_IndirectLightingRaw[pixelPosition] = float4(0.0, 0.0, 0.0, 1.0);
        }
		// Keep the debug decorrelation-factor viz clean for invalid pixels.
		if (g_Const.debug.visualizePTDecorrelationFactor)
			u_PTDecorrelationFactor[pixelPosition] = 0.0f;
		return;
	}

	ShaderDebug::SetDebugShaderPrintCurrentThreadCursorXY(pixelPosition);

	RTXDI_PTReservoir ptReservoir = RTXDI_LoadPTReservoir(g_Const.restirPT.reservoirBuffer, reservoirPosition, g_Const.restirPT.bufferIndices.finalShadingInputBufferIndex);

	const float scaledDecorrelationFactor = RTXDI_PTApplyDecorrelation(
		pixelPosition,
		reservoirPosition,
		ptReservoir,
		g_Const.restirPT.decorrelation,
		g_Const.restirPT.bufferIndices,
		g_Const.restirPT.reservoirBuffer,
		g_Const.runtimeParams.frameIndex);
	if (g_Const.debug.visualizePTDecorrelationFactor)
		u_PTDecorrelationFactor[pixelPosition] = scaledDecorrelationFactor;


	float3 diffuse = float3(0.0, 0.0, 0.0);
	float3 specular = float3(0.0, 0.0, 0.0);

	// demodulation for PSR

    float3 PSRDiffuseAlbedo = Unpack_R11G11B10_UFLOAT(u_PSRDiffuseAlbedo[pixelPosition]);
    float3 PSRSpecularF0 = Unpack_R11G11B10_UFLOAT(u_PSRSpecularF0[pixelPosition]);
    bool usePSR = (any(PSRDiffuseAlbedo > 0.f) || any(PSRSpecularF0 > 0.f));

    // Use PSR surface data only when PSR buffers were written (non-zero); otherwise use default G-buffer (primarySurface)
    if (usePSR)
    {
        SplitBrdf split;

        float3 specularUsed = (all(PSRSpecularF0 == 0.f) ? primarySurface.material.specularF0 : PSRSpecularF0);

        if (u_PSRLightDir[pixelPosition] != 0)
        {
            float metalness = GetMetalness(primarySurface.material.diffuseAlbedo, primarySurface.material.specularF0);
            PSRDiffuseAlbedo = lerp(primarySurface.material.diffuseAlbedo, PSRDiffuseAlbedo, metalness);
            PSRSpecularF0 = lerp(primarySurface.material.specularF0, PSRSpecularF0, metalness);

            float4 PSREncodedNormalRoughness = u_PSRNormalRoughness[pixelPosition];
            float3 PSRNormal = 2 * PSREncodedNormalRoughness.xyz - 1;
            float PSRRoughness = PSREncodedNormalRoughness.w;
            float3 PSRLightDir = octToNdirUnorm32(u_PSRLightDir[pixelPosition]);

            RAB_Surface PSRSurface = (RAB_Surface)0;
            PSRSurface.normal = PSRNormal;
            PSRSurface.material.roughness = PSRRoughness;
            PSRSurface.material.specularF0 = specularUsed;

            const float3 V = RAB_GetSurfaceViewDir(primarySurface);

            split = EvaluateBrdfPT(PSRSurface, PSRNormal, V, PSRLightDir);
        }
        else
        {
            split.demodulatedDiffuse = 0;
            split.specular = 1;
        }

        float3 diffuseUsed = (all(PSRDiffuseAlbedo == 0) ? primarySurface.material.diffuseAlbedo : PSRDiffuseAlbedo);
        float3 pHatUCW = ptReservoir.weightSum * ptReservoir.targetFunction;
        float3 totalBrdf = split.demodulatedDiffuse * diffuseUsed + split.specular;
        diffuse = (all(PSRDiffuseAlbedo == 0) ? 0.f : split.demodulatedDiffuse) * pHatUCW / totalBrdf; // demodulated
        specular = split.specular * pHatUCW / totalBrdf;
        diffuse = InvalidPixelGuard(diffuse);
        specular = DemodulateSpecular(specularUsed, specular);
        specular = InvalidPixelGuard(specular);
    }
    else
    {
        SplitBrdf lightingSample = CalculateDirectLighting(primarySurface, ptReservoir);
        CalculateDiffuseAndSpecular(lightingSample, primarySurface, ptReservoir, diffuse, specular);
        specular = DemodulateSpecular(primarySurface.material.specularF0, specular);
    }

    // When usePSR: use PSR depth/roughness/lightDistance; otherwise use primary G-buffer
    float viewDepthForOutput = usePSR ? u_PSRDepth[pixelPosition] : primarySurface.viewDepth;
    float roughnessForOutput = usePSR ? u_PSRNormalRoughness[pixelPosition].w : primarySurface.material.roughness;
    float hitT = u_PSRHitT[pixelPosition];
    bool isFirstPass = usePSR && (u_PSRDepth[pixelPosition] > primarySurface.viewDepth);

    // This pixel is a PSR surface if its depth is greater than the GBuffer value
    // If so, that means that ReSTIR DI did not initially update the NRD buffers with
    //   lighting for this pixel, which means ReSTIR PT is the first.
    bool isFirstPassSpecular = (g_Const.directLightingMode == DIRECT_LIGHTING_MODE_NONE) || u_PSRDepth[pixelPosition] > primarySurface.viewDepth; // always replace specular with PSR specular
    bool isFirstPassDiffuse = (g_Const.directLightingMode == DIRECT_LIGHTING_MODE_NONE) || (isFirstPassSpecular && all(primarySurface.material.diffuseAlbedo == 0.f)); // ReSTIR DI already writes the diffuse component so PSR diffuse might not be the first

    StoreShadingOutput(globalIndex, pixelPosition, viewDepthForOutput, roughnessForOutput, diffuse, specular, hitT, hitT, isFirstPassDiffuse, isFirstPassSpecular, true);

    // Non-PSR pixels: write default G-buffer values to PSR buffers so NRD sees correct depth/normal/roughness/motion
    if (!usePSR)
    {
        u_PSRDiffuseAlbedo[pixelPosition] = Pack_R11G11B10_UFLOAT(primarySurface.material.diffuseAlbedo);
        u_PSRSpecularF0[pixelPosition] = Pack_R11G11B10_UFLOAT(primarySurface.material.specularF0);

        u_PSRDepth[pixelPosition] = primarySurface.viewDepth;
        u_PSRNormalRoughness[pixelPosition] = float4(primarySurface.normal * 0.5 + 0.5, primarySurface.material.roughness);
        u_PSRMotionVectors[pixelPosition] = t_MotionVectors[pixelPosition];
    }

    if (g_Const.debug.outputDebugIndirectLighting)
    {
        u_IndirectLightingRaw[pixelPosition] = float4(diffuse + specular, 1.0);
    }
}

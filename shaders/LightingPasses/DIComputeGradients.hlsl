/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma pack_matrix(row_major)

#include "RtxdiApplicationBridge.hlsli"

#include <rtxdi/DIResamplingFunctions.hlsli>

#ifdef WITH_NRD
#undef WITH_NRD
#endif

#include "ShadingHelpers.hlsli"

#if USE_RAY_QUERY
[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 GlobalIndex : SV_DispatchThreadID, uint2 LocalIndex : SV_GroupThreadID, uint2 GroupIdx : SV_GroupID)
#else
[shader("raygeneration")]
void RayGen()
#endif
{
#if !USE_RAY_QUERY
    uint2 GlobalIndex = DispatchRaysIndex().xy;
#endif

    const RTXDI_RuntimeParameters params = g_Const.runtimeParams;

    // This shader runs one thread per image stratum, i.e. a square of pixels
    // (RTXDI_GRAD_FACTOR x RTXDI_GRAD_FACTOR) in size. One pixel is selected to
    // produce a gradient.

    bool usePrevSample = false;
    int2 selectedPixelPos = -1;
    int2 selectedPrevPixelPos = -1;
    float2 selectedDiffSpecLum = 0;
    
    // Iterate over all the pixels in the stratum, find one that is likely to produce
    // the brightest gradient. That means the pixel that has the brightest color coming
    // purely from light sampling in either the current or the previous frame.
    for (int yy = 0; yy < RTXDI_GRAD_FACTOR; yy++)
    for (int xx = 0; xx < RTXDI_GRAD_FACTOR; xx++)
    {
        // Translate the gradient stratum index (GlobalIndex) into reservoir and pixel positions.
        int2 srcReservoirPos = GlobalIndex * RTXDI_GRAD_FACTOR + int2(xx, yy);
        int2 srcPixelPos = RTXDI_ReservoirPosToPixelPos(srcReservoirPos, params.activeCheckerboardField);

        if (any(srcPixelPos >= int2(g_Const.view.viewportSize)))
            continue;

        // Find the matching pixel in the previous frame - that information is produced
        // by the temporal resampling or fused resampling shaders.
        int2 temporalPixelPos = u_TemporalSamplePositions[srcReservoirPos];
        int2 temporalReservoirPos = RTXDI_PixelPosToReservoirPos(temporalPixelPos, params.activeCheckerboardField);

        // Load the previous frame sampled lighting luminance.
        // For invalid gradients, temporalPixelPos is negative, and prevLuminance will be 0
        float2 prevLuminance = t_PrevRestirLuminance[temporalReservoirPos];
        
        // Load the current frame sampled lighting luminance.
        float2 currLuminance = u_RestirLuminance[srcReservoirPos];
        
        float currMaxLuminance = max(currLuminance.x, currLuminance.y);
        float prevMaxLuminance = max(prevLuminance.x, prevLuminance.y);
        float selectedMaxLuminance = max(selectedDiffSpecLum.x, selectedDiffSpecLum.y);

        // Feed the brightest of (current, previous) samples into the gradient pixel selection logic.
        if (currMaxLuminance > selectedMaxLuminance && currMaxLuminance > prevMaxLuminance)
        {
            usePrevSample = false;
            selectedPixelPos = srcPixelPos;
            selectedPrevPixelPos = temporalPixelPos;
            selectedDiffSpecLum = currLuminance;
        }
        else if (prevMaxLuminance > selectedMaxLuminance)
        {
            usePrevSample = true;
            selectedPixelPos = srcPixelPos;
            selectedPrevPixelPos = temporalPixelPos;
            selectedDiffSpecLum = prevLuminance;
        }
    }

    float4 gradient = 0;

    // If we have found at least one non-black pixel in the loop above...
    if (selectedDiffSpecLum.x > 0 || selectedDiffSpecLum.y > 0)
    {
        int2 selectedCurrentOrPrevPixelPos = usePrevSample ? selectedPrevPixelPos : selectedPixelPos;

        // Translate the pixel pos into reservoir pos - the math the same for both current and prev frames,
        // unlike the reverse translation that has to take the active checkerboard field into account.
        int2 selectedCurrentOrPrevReservoirPos = RTXDI_PixelPosToReservoirPos(selectedCurrentOrPrevPixelPos, params.activeCheckerboardField);

        // Load the reservoir that was selected for gradient evaluation, either from the current or the previous frame.
        RTXDI_DIReservoir selectedReservoir = RTXDI_LoadDIReservoir(g_Const.restirDI.reservoirBufferParams,
            selectedCurrentOrPrevReservoirPos,
            usePrevSample ? g_Const.restirDI.bufferIndices.temporalResamplingInputBufferIndex : g_Const.restirDI.bufferIndices.shadingInputBufferIndex);

        // Map the reservoir's light index into the other frame (previous or current)
        int selectedMappedLightIndex = RAB_TranslateLightIndex(RTXDI_GetDIReservoirLightIndex(selectedReservoir), !usePrevSample);
        
        if (selectedMappedLightIndex >= 0)
        {
            // If the mapping was successful, compare the lighting.

            // Load the current G-buffer surface
            RAB_Surface surface = RAB_GetGBufferSurface(selectedPixelPos, false);

            // Find the world-space motion of the current surface
            float3 motionVector = t_MotionVectors[selectedPixelPos].xyz;
            motionVector.xy += g_Const.prevView.pixelOffset - g_Const.view.pixelOffset;

            motionVector = convertMotionVectorToPixelSpace(g_Const.view, g_Const.prevView, selectedPixelPos, motionVector);

            float3 prevWorldPos = getPreviousWorldPos(g_Const.prevView, selectedPixelPos, surface.viewDepth, motionVector);
            float3 worldMotion = surface.worldPos - prevWorldPos;

            if (usePrevSample)
            {
                // If the gradient is from the previous frame, use the previous surface for shading,
                // but reconstruct the new position using worldMotion to avoid false-positive gradients
                // and self-occlusion from imperfectly matching G-buffer surfaces.
                surface = RAB_GetGBufferSurface(selectedPrevPixelPos, true);
                surface.worldPos += worldMotion;
            }
            else
            {
                // If the gradient is from the current frame, use the current surface for shading,
                // and reconstruct the previous world position using the motion vectors.
                surface.worldPos = prevWorldPos;
            }

            // Reconstruct the light sample
            RAB_LightInfo lightInfo = RAB_LoadLightInfo(selectedMappedLightIndex, !usePrevSample);
            RAB_LightSample lightSample = RAB_SamplePolymorphicLight(lightInfo,
                surface, RTXDI_GetDIReservoirSampleUV(selectedReservoir));

            // Shade the other (previous or current) surface using the other light sample
            float3 diffuse = 0;
            float3 specular = 0;
            float lightDistance = 0;
            ShadeSurfaceWithLightSample(selectedReservoir, surface, lightSample,
                /* previousFrameTLAS = */ !usePrevSample, /* enableVisibilityReuse = */ false, diffuse, specular, lightDistance);

            // Calculate the sampled lighting luminance for the other surface
            float2 newDiffSpecLum = float2(calcLuminance(diffuse * surface.diffuseAlbedo), calcLuminance(specular));

            // Convert to FP16 and back to avoid false-positive gradients due to precision loss in the 
            // u_RestirLuminance and t_PrevRestirLuminance textures where selectedDiffSpecLum comes from.
            newDiffSpecLum = f16tof32(f32tof16(newDiffSpecLum));

            // Compute the gradient
            gradient.xy = abs(selectedDiffSpecLum - newDiffSpecLum);
            gradient.zw = max(selectedDiffSpecLum, newDiffSpecLum);
        }
        else
        {
            // Light index mapping was unsuccessful, which means the light has either appeared or disappeared.
            // Which means the gradient is 100% of the pixel's luminance.
            gradient.xy = selectedDiffSpecLum;
            gradient.zw = selectedDiffSpecLum;
        }
    }

    // Store the output
    u_Gradients[int3(GlobalIndex, 0)] = min(gradient * RTXDI_GRAD_STORAGE_SCALE, RTXDI_GRAD_MAX_VALUE);
}
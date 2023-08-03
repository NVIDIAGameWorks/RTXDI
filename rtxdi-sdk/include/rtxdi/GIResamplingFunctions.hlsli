/***************************************************************************
 # Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#ifndef GI_RESAMPLING_FUNCTIONS_HLSLI
#define GI_RESAMPLING_FUNCTIONS_HLSLI

#include "GIReservoir.hlsli"

// These macros can be defined in the including shader file to reduce code bloat
// and/or remove ray tracing calls from spatial resampling shaders
// if bias correction is not necessary.

// Enabled by default. Application code need to define those macros appropriately to optimize shaders.
#ifndef RTXDI_GI_ALLOWED_BIAS_CORRECTION
#define RTXDI_GI_ALLOWED_BIAS_CORRECTION RTXDI_BIAS_CORRECTION_RAY_TRACED
#endif

// Adds `newReservoir` into `reservoir`, returns true if the new reservoir's sample was selected.
// This function assumes the newReservoir has been normalized, so its weightSum means "1/g * 1/M * \sum{g/p}"
// and the targetPdf is a conversion factor from the newReservoir's space to the reservoir's space (integrand).
bool RTXDI_CombineGIReservoirs(
    inout RTXDI_GIReservoir reservoir,
    const RTXDI_GIReservoir newReservoir,
    float random,
    float targetPdf)
{
    // What's the current weight (times any prior-step RIS normalization factor)
    const float risWeight = targetPdf * newReservoir.weightSum * newReservoir.M;

    // Our *effective* candidate pool is the sum of our candidates plus those of our neighbors
    reservoir.M += newReservoir.M;

    // Update the weight sum
    reservoir.weightSum += risWeight;

    // Decide if we will randomly pick this sample
    bool selectSample = (random * reservoir.weightSum <= risWeight);

    if (selectSample)
    {
        reservoir.position = newReservoir.position;
        reservoir.normal = newReservoir.normal;
        reservoir.radiance = newReservoir.radiance;
        reservoir.age = newReservoir.age;
    }

    return selectSample;
}

// Performs normalization of the reservoir after streaming.
void RTXDI_FinalizeGIResampling(
    inout RTXDI_GIReservoir reservoir,
    float normalizationNumerator,
    float normalizationDenominator)
{
    reservoir.weightSum = (normalizationDenominator == 0.0) ? 0.0 : (reservoir.weightSum * normalizationNumerator) / normalizationDenominator;
}

// Calculate the elements of the Jacobian to transform the sample's solid angle.
void RTXDI_CalculatePartialJacobian(const float3 recieverPos, const float3 samplePos, const float3 sampleNormal,
    out float distanceToSurface, out float cosineEmissionAngle)
{
    float3 vec = recieverPos - samplePos;

    distanceToSurface = length(vec);
    cosineEmissionAngle = saturate(dot(sampleNormal, vec / distanceToSurface));
}

// Calculates the full Jacobian for resampling neighborReservoir into a new receiver surface
float RTXDI_CalculateJacobian(float3 recieverPos, float3 neighborReceiverPos, const RTXDI_GIReservoir neighborReservoir)
{
    // Calculate Jacobian determinant to adjust weight.
    // See Equation (11) in the ReSTIR GI paper.
    float originalDistance, originalCosine;
    float newDistance, newCosine;
    RTXDI_CalculatePartialJacobian(recieverPos, neighborReservoir.position, neighborReservoir.normal, newDistance, newCosine);
    RTXDI_CalculatePartialJacobian(neighborReceiverPos, neighborReservoir.position, neighborReservoir.normal, originalDistance, originalCosine);

    float jacobian = (newCosine * originalDistance * originalDistance)
        / (originalCosine * newDistance * newDistance);

    if (isinf(jacobian) || isnan(jacobian))
        jacobian = 0;

    return jacobian;
}

// Creates a GI reservoir from a raw light sample.
// Note: the original sample PDF can be embedded into sampleRadiance, in which case the samplePdf parameter should be set to 1.0.
RTXDI_GIReservoir RTXDI_MakeGIReservoir(
    const float3 samplePos,
    const float3 sampleNormal,
    const float3 sampleRadiance,
    const float samplePdf)
{
    RTXDI_GIReservoir reservoir;
    reservoir.position = samplePos;
    reservoir.normal = sampleNormal;
    reservoir.radiance = sampleRadiance;
    reservoir.weightSum = samplePdf > 0.0 ? 1.0 / samplePdf : 0.0;
    reservoir.M = 1;
    reservoir.age = 0;
    return reservoir;
}

// Generates a pattern of offsets for looking closely around a given pixel.
// The pattern places 'sampleIdx' at the following locations in screen space around pixel (x):
//   0 4 3
//   6 x 7
//   2 5 1
int2 RTXDI_CalculateTemporalResamplingOffset(int sampleIdx, int radius)
{
    sampleIdx &= 7;

    int mask2 = sampleIdx >> 1 & 0x01;       // 0, 0, 1, 1, 0, 0, 1, 1
    int mask4 = 1 - (sampleIdx >> 2 & 0x01); // 1, 1, 1, 1, 0, 0, 0, 0
    int tmp0 = -1 + 2 * (sampleIdx & 0x01);  // -1, 1,....
    int tmp1 = 1 - 2 * mask2;                // 1, 1,-1,-1, 1, 1,-1,-1
    int tmp2 = mask4 | mask2;                // 1, 1, 1, 1, 0, 0, 1, 1
    int tmp3 = mask4 | (1 - mask2);          // 1, 1, 1, 1, 1, 1, 0, 0

    return int2(tmp0, tmp0 * tmp1) * int2(tmp2, tmp3) * radius;
}

int2 RTXDI_CalculateSpatialResamplingOffset(int sampleIdx, float radius, const uint neighborOffsetMask)
{
    sampleIdx &= int(neighborOffsetMask);
    return int2(float2(RTXDI_NEIGHBOR_OFFSETS_BUFFER[sampleIdx].xy) * radius);
}

// A structure that groups the application-provided settings for spatio-temporal resampling.
struct RTXDI_GITemporalResamplingParameters
{
    // Screen-space motion vector, computed as (previousPosition - currentPosition).
    // The X and Y components are measured in pixels.
    // The Z component is in linear depth units.
    float3 screenSpaceMotion;

    // The index of the reservoir buffer to pull the temporal samples from.
    uint sourceBufferIndex;

    // Maximum history length for reuse, measured in frames.
    // Higher values result in more stable and high quality sampling, at the cost of slow reaction to changes.
    uint maxHistoryLength;

    // Controls the bias correction math for temporal reuse. Depending on the setting, it can add
    // some shader cost and one approximate shadow ray per pixel (or per two pixels if checkerboard sampling is enabled).
    // Ideally, these rays should be traced through the previous frame's BVH to get fully unbiased results.
    uint biasCorrectionMode;

    // Surface depth similarity threshold for temporal reuse.
    // If the previous frame surface's depth is within this threshold from the current frame surface's depth,
    // the surfaces are considered similar. The threshold is relative, i.e. 0.1 means 10% of the current depth.
    // Otherwise, the pixel is not reused, and the resampling shader will look for a different one.
    float depthThreshold;

    // Surface normal similarity threshold for temporal reuse.
    // If the dot product of two surfaces' normals is higher than this threshold, the surfaces are considered similar.
    // Otherwise, the pixel is not reused, and the resampling shader will look for a different one.
    float normalThreshold;

    // Discard the reservoir if its age exceeds this value.
    uint maxReservoirAge;

    // Enables permuting the pixels sampled from the previous frame in order to add temporal
    // variation to the output signal and make it more denoiser friendly.
    bool enablePermutationSampling;

    // Enables resampling from a location around the current pixel instead of what the motion vector points at,
    // in case no surface near the motion vector matches the current surface (e.g. disocclusion).
    // This behavoir makes disocclusion areas less noisy but locally biased, usually darker.
    bool enableFallbackSampling;

    // Random number for permutation sampling that is the same for all pixels in the frame
    uint uniformRandomNumber;
};

// Temporal resampling for GI reservoir pass.
RTXDI_GIReservoir RTXDI_GITemporalResampling(
    const uint2 pixelPosition,
    const RAB_Surface surface,
    const RTXDI_GIReservoir inputReservoir,
    inout RAB_RandomSamplerState rng,
    const RTXDI_RuntimeParameters params,
    const RTXDI_ReservoirBufferParameters reservoirParams,
    const RTXDI_GITemporalResamplingParameters tparams)
{
    // Backproject this pixel to last frame
    int2 prevPos = int2(round(float2(pixelPosition) + tparams.screenSpaceMotion.xy));
    const float expectedPrevLinearDepth = RAB_GetSurfaceLinearDepth(surface) + tparams.screenSpaceMotion.z;
    const int radius = (params.activeCheckerboardField == 0) ? 1 : 2;

    RTXDI_GIReservoir temporalReservoir;
    bool foundTemporalReservoir = false;

    const int temporalSampleStartIdx = int(RAB_GetNextRandom(rng) * 8);

    RAB_Surface temporalSurface = RAB_EmptySurface();

    // Try to find a matching surface in the neighborhood of the reprojected pixel
    const int temporalSampleCount = 5;
    const int sampleCount = temporalSampleCount + (tparams.enableFallbackSampling ? 1 : 0);
    for (int i = 0; i < sampleCount; i++)
    {
        const bool isFirstSample = i == 0;
        const bool isFallbackSample = i == temporalSampleCount;

        int2 offset = int2(0, 0);
        if (isFallbackSample)
        {
            // Last sample is a fallback for disocclusion areas: use zero motion vector.
            prevPos = int2(pixelPosition);
        }
        else if (!isFirstSample)
        {
            offset = RTXDI_CalculateTemporalResamplingOffset(temporalSampleStartIdx + i, radius);
        }

        int2 idx = prevPos + offset;
        if ((tparams.enablePermutationSampling && isFirstSample) || isFallbackSample)
        {
            // Apply permutation sampling for the first (non-jittered) sample,
            // also for the last (fallback) sample to prevent visible repeating patterns in disocclusions.
            RTXDI_ApplyPermutationSampling(idx, tparams.uniformRandomNumber);
        }

        RTXDI_ActivateCheckerboardPixel(idx, true, params.activeCheckerboardField);
        
        // Grab shading / g-buffer data from last frame
        temporalSurface = RAB_GetGBufferSurface(idx, true);

        if (!RAB_IsSurfaceValid(temporalSurface))
        {
            continue;
        }

        // Test surface similarity, discard the sample if the surface is too different.
        // Skip this test for the last (fallback) sample.
        if (!isFallbackSample && !RTXDI_IsValidNeighbor(
            RAB_GetSurfaceNormal(surface), RAB_GetSurfaceNormal(temporalSurface),
            expectedPrevLinearDepth, RAB_GetSurfaceLinearDepth(temporalSurface),
            tparams.normalThreshold, tparams.depthThreshold))
        {
            continue;
        }

        // Test material similarity and perform any other app-specific tests.
        if (!RAB_AreMaterialsSimilar(surface, temporalSurface))
        {
            continue;
        }

        // Read temporal reservoir.
        uint2 prevReservoirPos = RTXDI_PixelPosToReservoirPos(idx, params.activeCheckerboardField);
        temporalReservoir = RTXDI_LoadGIReservoir(reservoirParams, prevReservoirPos, tparams.sourceBufferIndex);

        // Check if the reservoir is a valid one.
        if (!RTXDI_IsValidGIReservoir(temporalReservoir))
        {
            continue;
        }

        foundTemporalReservoir = true;
        break;
    }

    RTXDI_GIReservoir curReservoir = RTXDI_EmptyGIReservoir();

    float selectedTargetPdf = 0;
    if (RTXDI_IsValidGIReservoir(inputReservoir))
    {
        selectedTargetPdf = RAB_GetGISampleTargetPdfForSurface(inputReservoir.position, inputReservoir.radiance, surface);

        RTXDI_CombineGIReservoirs(curReservoir, inputReservoir, /* random = */ 0.5, selectedTargetPdf);
    }
    
    if (foundTemporalReservoir)
    {
        // Found a valid temporal surface and its GI reservoir.

        // Calculate Jacobian determinant to adjust weight.
        float jacobian = RTXDI_CalculateJacobian(RAB_GetSurfaceWorldPos(surface), RAB_GetSurfaceWorldPos(temporalSurface), temporalReservoir);

        if (!RAB_ValidateGISampleWithJacobian(jacobian))
            foundTemporalReservoir = false;

        temporalReservoir.weightSum *= jacobian;
        
        // Clamp history length
        temporalReservoir.M = min(temporalReservoir.M, tparams.maxHistoryLength);

        // Make the sample older
        ++temporalReservoir.age;

        if (temporalReservoir.age > tparams.maxReservoirAge)
            foundTemporalReservoir = false;
    }

    bool selectedPreviousSample = false;
    if (foundTemporalReservoir)
    {
        // Reweighting and denormalize the temporal sample with the current surface.
        float targetPdf = RAB_GetGISampleTargetPdfForSurface(temporalReservoir.position, temporalReservoir.radiance, surface);
        
        // Combine the temporalReservoir into the curReservoir
        selectedPreviousSample = RTXDI_CombineGIReservoirs(curReservoir, temporalReservoir, RAB_GetNextRandom(rng), targetPdf);
        if (selectedPreviousSample)
        {
            selectedTargetPdf = targetPdf;
        }
    }

#if RTXDI_GI_ALLOWED_BIAS_CORRECTION >= RTXDI_BIAS_CORRECTION_BASIC
    if (tparams.biasCorrectionMode >= RTXDI_BIAS_CORRECTION_BASIC)
    {
        float pi = selectedTargetPdf;
        float piSum = selectedTargetPdf * inputReservoir.M;

        if (RTXDI_IsValidGIReservoir(curReservoir) && foundTemporalReservoir)
        {
            float temporalP = RAB_GetGISampleTargetPdfForSurface(curReservoir.position, curReservoir.radiance, temporalSurface);

#if RTXDI_GI_ALLOWED_BIAS_CORRECTION >= RTXDI_BIAS_CORRECTION_RAY_TRACED
            if (tparams.biasCorrectionMode == RTXDI_BIAS_CORRECTION_RAY_TRACED && temporalP > 0)
            {
                if (!RAB_GetTemporalConservativeVisibility(surface, temporalSurface, curReservoir.position))
                {
                    temporalP = 0;
                }
            }
#endif

            pi = selectedPreviousSample ? temporalP : pi;
            piSum += temporalP * temporalReservoir.M;
        }

        // Normalizing
        float normalizationNumerator = pi;
        float normalizationDenominator = piSum * selectedTargetPdf;
        RTXDI_FinalizeGIResampling(curReservoir, normalizationNumerator, normalizationDenominator);
    }
    else
#endif
    {
        // Normalizing
        float normalizationNumerator = 1.0;
        float normalizationDenominator = selectedTargetPdf * curReservoir.M;
        RTXDI_FinalizeGIResampling(curReservoir, normalizationNumerator, normalizationDenominator);
    }

    return curReservoir;
}

// A structure that groups the application-provided settings for spatial resampling.
struct RTXDI_GISpatialResamplingParameters
{
    // The index of the reservoir buffer to pull the spatial samples from.
    uint sourceBufferIndex;

    // Surface depth similarity threshold for temporal reuse.
    // If the previous frame surface's depth is within this threshold from the current frame surface's depth,
    // the surfaces are considered similar. The threshold is relative, i.e. 0.1 means 10% of the current depth.
    // Otherwise, the pixel is not reused, and the resampling shader will look for a different one.
    float depthThreshold;

    // Surface normal similarity threshold for temporal reuse.
    // If the dot product of two surfaces' normals is higher than this threshold, the surfaces are considered similar.
    // Otherwise, the pixel is not reused, and the resampling shader will look for a different one.
    float normalThreshold;

    // Number of neighbor pixels considered for resampling (1-32)
    // Some of the may be skipped if they fail the surface similarity test.
    uint numSamples;

    // Screen-space radius for spatial resampling, measured in pixels.
    float samplingRadius;

    // Controls the bias correction math for temporal reuse. Depending on the setting, it can add
    // some shader cost and one approximate shadow ray per pixel (or per two pixels if checkerboard sampling is enabled).
    // Ideally, these rays should be traced through the previous frame's BVH to get fully unbiased results.
    uint biasCorrectionMode;
};

RTXDI_GIReservoir RTXDI_GISpatialResampling(
    const uint2 pixelPosition,
    const RAB_Surface surface,
    const RTXDI_GIReservoir inputReservoir,
    inout RAB_RandomSamplerState rng,
    const RTXDI_RuntimeParameters params,
    const RTXDI_ReservoirBufferParameters reservoirParams,
    const RTXDI_GISpatialResamplingParameters sparams)
{
    const uint numSamples = sparams.numSamples;

    // The current reservoir.
    RTXDI_GIReservoir curReservoir = RTXDI_EmptyGIReservoir();

    float selectedTargetPdf = 0;
    if (RTXDI_IsValidGIReservoir(inputReservoir)) {
        selectedTargetPdf = RAB_GetGISampleTargetPdfForSurface(inputReservoir.position, inputReservoir.radiance, surface);

        RTXDI_CombineGIReservoirs(curReservoir, inputReservoir, /* random = */ 0.5, selectedTargetPdf);
    }

    // We loop through neighbors twice if bias correction is enabled.  Cache the validity / edge-stopping function
    // results for the 2nd time through.
    uint cachedResult = 0;

    // Since we're using our bias correction scheme, we need to remember which light selection we made
    int selected = -1;

    const int neighborSampleStartIdx = int(RAB_GetNextRandom(rng) * params.neighborOffsetMask);

    // Walk the specified number of spatial neighbors, resampling using RIS
    for (int i = 0; i < numSamples; ++i)
    {
        // Get screen-space location of neighbor
        int2 idx = int2(pixelPosition) + RTXDI_CalculateSpatialResamplingOffset(neighborSampleStartIdx + i, sparams.samplingRadius, params.neighborOffsetMask);

        idx = RAB_ClampSamplePositionIntoView(idx, false);

        RTXDI_ActivateCheckerboardPixel(idx, false, params.activeCheckerboardField);

        RAB_Surface neighborSurface = RAB_GetGBufferSurface(idx, false);

        // Test surface similarity, discard the sample if the surface is too different.
        if (!RTXDI_IsValidNeighbor(
            RAB_GetSurfaceNormal(surface), RAB_GetSurfaceNormal(neighborSurface),
            RAB_GetSurfaceLinearDepth(surface), RAB_GetSurfaceLinearDepth(neighborSurface),
            sparams.normalThreshold, sparams.depthThreshold))
        {
            continue;
        }

        // Test material similarity and perform any other app-specific tests.
        if (!RAB_AreMaterialsSimilar(surface, neighborSurface))
        {
            continue;
        }

        const uint2 neighborReservoirPos = RTXDI_PixelPosToReservoirPos(idx, params.activeCheckerboardField);
        RTXDI_GIReservoir neighborReservoir = RTXDI_LoadGIReservoir(reservoirParams, neighborReservoirPos, sparams.sourceBufferIndex);

        if (!RTXDI_IsValidGIReservoir(neighborReservoir))
        {
            continue;
        }

        // Calculate Jacobian determinant to adjust weight.
        float jacobian = RTXDI_CalculateJacobian(RAB_GetSurfaceWorldPos(surface), RAB_GetSurfaceWorldPos(neighborSurface), neighborReservoir);

        // Compute reuse weight.
        float targetPdf = RAB_GetGISampleTargetPdfForSurface(neighborReservoir.position, neighborReservoir.radiance, surface);

        // The Jacobian to transform a GI sample's solid angle holds the lengths and angles to the GI sample from the surfaces,
        // that are valuable information to determine if the GI sample should be combined with the current sample's stream.
        // This function also may clamp the value of the Jacobian.
        if (!RAB_ValidateGISampleWithJacobian(jacobian))
        {
            continue;
        }

        // Valid neighbor surface and its GI reservoir. Combine the reservor.
        cachedResult |= (1u << uint(i));

        // Combine
        bool isUpdated = RTXDI_CombineGIReservoirs(curReservoir, neighborReservoir, RAB_GetNextRandom(rng), targetPdf * jacobian);
        if (isUpdated) {
            selected = i;
            selectedTargetPdf = targetPdf;
        }
    }

#if RTXDI_GI_ALLOWED_BIAS_CORRECTION >= RTXDI_BIAS_CORRECTION_BASIC
    if (sparams.biasCorrectionMode >= RTXDI_BIAS_CORRECTION_BASIC)
    {
        // Compute the unbiased normalization factor (instead of using 1/M)
        float pi = selectedTargetPdf;
        float piSum = selectedTargetPdf * inputReservoir.M;

        // If the GI reservoir has selected other than the initial sample, the position should be come from the previous frame.
        // However, there is no idea for the previous position of the initial GI reservoir, so it just uses the current position as its previous one.
        // float3 selectedPositionInPreviousFrame = curReservoir.position;

        // We need to walk our neighbors again
        for (int i = 0; i < numSamples; ++i)
        {
            // If we skipped this neighbor above, do so again.
            if ((cachedResult & (1u << uint(i))) == 0) continue;

            // Get the screen-space location of our neighbor
            int2 idx = int2(pixelPosition) + RTXDI_CalculateSpatialResamplingOffset(neighborSampleStartIdx + i, sparams.samplingRadius, params.neighborOffsetMask);

            idx = RAB_ClampSamplePositionIntoView(idx, false);

            RTXDI_ActivateCheckerboardPixel(idx, false, params.activeCheckerboardField);

            // Load our neighbor's G-buffer and its GI reservoir again.
            RAB_Surface neighborSurface = RAB_GetGBufferSurface(idx, false);

            const uint2 neighborReservoirPos = RTXDI_PixelPosToReservoirPos(idx, params.activeCheckerboardField);
            RTXDI_GIReservoir neighborReservoir = RTXDI_LoadGIReservoir(reservoirParams, neighborReservoirPos, sparams.sourceBufferIndex);

            // Get the PDF of the sample RIS selected in the first loop, above, *at this neighbor*
            float ps = RAB_GetGISampleTargetPdfForSurface(curReservoir.position, curReservoir.radiance, neighborSurface);

            // This should be done to correct bias.
#if RTXDI_GI_ALLOWED_BIAS_CORRECTION >= RTXDI_BIAS_CORRECTION_RAY_TRACED
            if (sparams.biasCorrectionMode == RTXDI_BIAS_CORRECTION_RAY_TRACED && ps > 0)
            {
                if (!RAB_GetConservativeVisibility(neighborSurface, curReservoir.position))
                {
                    ps = 0;
                }
            }
#endif
            // Select this sample for the (normalization) numerator if this particular neighbor pixel
            // was the one we selected via RIS in the first loop, above.
            pi = selected == i ? ps : pi;

            // Add to the sums of weights for the (normalization) denominator
            piSum += ps * neighborReservoir.M;
        }

        // "MIS-like" normalization
        // {wSum * (pi/piSum)} * 1/selectedTargetPdf
        {
            float normalizationNumerator = pi;
            float normalizationDenominator = selectedTargetPdf * piSum;
            RTXDI_FinalizeGIResampling(curReservoir, normalizationNumerator, normalizationDenominator);
        }
    }
    else
#endif
    {
        // Normalization
        // {wSum * (1/ M)} * 1/selectedTargetPdf
        {
            float normalizationNumerator = 1.0;
            float normalizationDenominator = curReservoir.M * selectedTargetPdf;
            RTXDI_FinalizeGIResampling(curReservoir, normalizationNumerator, normalizationDenominator);
        }
    }

    return curReservoir;
}

// A structure that groups the application-provided settings for spatio-temporal resampling.
struct RTXDI_GISpatioTemporalResamplingParameters
{
    // Screen-space motion vector, computed as (previousPosition - currentPosition).
    // The X and Y components are measured in pixels.
    // The Z component is in linear depth units.
    float3 screenSpaceMotion;

    // The index of the reservoir buffer to pull the temporal and spatio-temporal samples from.
    // The first one is used as the source buffer for temporal filter, and the second one is used as the source of spatial filter.
    uint sourceBufferIndex;

    // Maximum history length for reuse, measured in frames.
    // Higher values result in more stable and high quality sampling, at the cost of slow reaction to changes.
    uint maxHistoryLength;

    // Surface depth similarity threshold for temporal reuse.
    // If the previous frame surface's depth is within this threshold from the current frame surface's depth,
    // the surfaces are considered similar. The threshold is relative, i.e. 0.1 means 10% of the current depth.
    // Otherwise, the pixel is not reused, and the resampling shader will look for a different one.
    float depthThreshold;

    // Surface normal similarity threshold for temporal reuse.
    // If the dot product of two surfaces' normals is higher than this threshold, the surfaces are considered similar.
    // Otherwise, the pixel is not reused, and the resampling shader will look for a different one.
    float normalThreshold;

    // Discard the reservoir if its age exceeds this value.
    uint maxReservoirAge;

    // Number of neighbor pixels considered for resampling (1-32)
    // Some of them may be skipped if they fail the surface similarity test.
    uint numSpatialSamples;

    // Screen-space radius for spatial resampling, measured in pixels.
    float samplingRadius;

    // Controls the bias correction math for temporal reuse. Depending on the setting, it can add
    // some shader cost and one approximate shadow ray per pixel (or per two pixels if checkerboard sampling is enabled).
    // Ideally, these rays should be traced through the previous frame's BVH to get fully unbiased results.
    // To enable bias correction mode, you must define RTXDI_GI_ALLOWED_BIAS_CORRECTION properly.
    uint biasCorrectionMode;

    // Enables permuting the pixels sampled from the previous frame in order to add temporal
    // variation to the output signal and make it more denoiser friendly.
    bool enablePermutationSampling;

    // Enables resampling from a location around the current pixel instead of what the motion vector points at,
    // in case no surface near the motion vector matches the current surface (e.g. disocclusion).
    // This behavior makes disocclusion areas less noisy but locally biased, usually darker.
    bool enableFallbackSampling;

    // Random number for permutation sampling that is the same for all pixels in the frame
    uint uniformRandomNumber;
};

RTXDI_GIReservoir RTXDI_GISpatioTemporalResampling(
    const uint2 pixelPosition,
    const RAB_Surface surface,
    RTXDI_GIReservoir inputReservoir,
    inout RAB_RandomSamplerState rng,
    const RTXDI_RuntimeParameters params,
    const RTXDI_ReservoirBufferParameters reservoirParams,
    const RTXDI_GISpatioTemporalResamplingParameters stparams)
{
    // Backproject this pixel to last frame
    int2 prevPos = int2(round(float2(pixelPosition) + stparams.screenSpaceMotion.xy));
    const float expectedPrevLinearDepth = RAB_GetSurfaceLinearDepth(surface) + stparams.screenSpaceMotion.z;

    // The current reservoir.
    RTXDI_GIReservoir curReservoir = RTXDI_EmptyGIReservoir();

    float selectedTargetPdf = 0;
    if (RTXDI_IsValidGIReservoir(inputReservoir))
    {
        selectedTargetPdf = RAB_GetGISampleTargetPdfForSurface(inputReservoir.position, inputReservoir.radiance, surface);

        RTXDI_CombineGIReservoirs(curReservoir, inputReservoir, /* random = */ 0.5, selectedTargetPdf);
    }

    // We loop through neighbors twice if bias correction is enabled.  Cache the validity / edge-stopping function
    // results for the 2nd time through.
    uint cachedResult = 0;

    // Since we're using our bias correction scheme, we need to remember which light selection we made
    int selected = -1;
    bool usingFallback = false;
    bool foundTemporalSurface = false;

    // The loop below jumps around the sample indices a bit to implement all the needed features:
    // 1. Temporal surface search, starting at sample 0 exactly at the motion vector, then a few
    //    samples around that location trying to find a matching surface.
    // 2. Fallback temporal reuse, in case step 1 failed. This step and further sampling 
    //    will switch to sampling around the current pixel position.
    // 3. Spatial reuse with multiple samples merged into the output - in contrast with temporal
    //    reuse that only ends up merging one sample.
    // The normalization loop later in this function relies on the mask produced in the first loop
    // to only consider the actually merged samples, and on the 'prevPos' value being modified
    // to support fallback sampling.
    const int temporalSampleCount = 5;
    const int fallbackSampleCount = 1;
    const int totalTemporalSampleCount = temporalSampleCount + fallbackSampleCount;
    const int totalSampleCount = min(totalTemporalSampleCount + int(stparams.numSpatialSamples), 32);

    const int temporalSampleStartIdx = int(RAB_GetNextRandom(rng) * 8);
    const int temporalJitterRadius = (params.activeCheckerboardField == 0) ? 1 : 2;
    const int neighborSampleStartIdx = int(RAB_GetNextRandom(rng) * params.neighborOffsetMask);

    // Walk the specified number of spatial neighbors, resampling using RIS
    for (int i = 0; i < totalSampleCount; ++i)
    {
        if (i < totalTemporalSampleCount && foundTemporalSurface)
        {
            // If we've just found a temporal surface, skip to the first spatial sample.
            i = totalTemporalSampleCount - 1;
            continue;
        }

        if (i == temporalSampleCount)
        {
            if (stparams.enableFallbackSampling)
            {
                // None of the temporal surfaces at the motion vector matched the current one,
                // switch to the fallback location.
                prevPos = int2(pixelPosition);
                usingFallback = true;
            }
            else
            {
                // Fallback disabled - skip to the first spatial sample
                i = totalTemporalSampleCount - 1;
                continue;
            }
        }

        const bool isFirstTemporalSample = i == 0;
        const bool isJitteredTemporalSample = i < temporalSampleCount;
        const bool isFallbackSample = i == temporalSampleCount;

        // Get screen-space location of neighbor
        int2 idx;
        if (isFirstTemporalSample || isFallbackSample)
        {
            idx = prevPos;

            if (stparams.enablePermutationSampling || isFallbackSample)
                RTXDI_ApplyPermutationSampling(idx, stparams.uniformRandomNumber);
        }
        else if (isJitteredTemporalSample)
        {
            idx = prevPos + RTXDI_CalculateTemporalResamplingOffset(temporalSampleStartIdx + i, temporalJitterRadius);
        }
        else
        {
            idx = prevPos + RTXDI_CalculateSpatialResamplingOffset(neighborSampleStartIdx + i, stparams.samplingRadius, params.neighborOffsetMask);
            idx = RAB_ClampSamplePositionIntoView(idx, true);
        }

        RTXDI_ActivateCheckerboardPixel(idx, true, params.activeCheckerboardField);

        RAB_Surface neighborSurface = RAB_GetGBufferSurface(idx, true);

        // Test surface similarity, discard the sample if the surface is too different.
        // Skip the test if we're sampling around the fallback location.
        if (!usingFallback && !RTXDI_IsValidNeighbor(
            RAB_GetSurfaceNormal(surface), RAB_GetSurfaceNormal(neighborSurface),
            expectedPrevLinearDepth, RAB_GetSurfaceLinearDepth(neighborSurface),
            stparams.normalThreshold, stparams.depthThreshold))
        {
            continue;
        }

        // Test material similarity and perform any other app-specific tests.
        if (!RAB_AreMaterialsSimilar(surface, neighborSurface))
        {
            continue;
        }

        const uint2 neighborReservoirPos = RTXDI_PixelPosToReservoirPos(idx, params.activeCheckerboardField);
        RTXDI_GIReservoir neighborReservoir = RTXDI_LoadGIReservoir(reservoirParams, neighborReservoirPos, stparams.sourceBufferIndex);

        if (!RTXDI_IsValidGIReservoir(neighborReservoir))
        {
            continue;
        }

        foundTemporalSurface = true;

        if (neighborReservoir.age >= stparams.maxReservoirAge)
        {
            continue;
        }

        // Calculate Jacobian determinant to adjust weight.
        float jacobian = RTXDI_CalculateJacobian(RAB_GetSurfaceWorldPos(surface), RAB_GetSurfaceWorldPos(neighborSurface), neighborReservoir);

        // Compute reuse weight.
        float targetPdf = RAB_GetGISampleTargetPdfForSurface(neighborReservoir.position, neighborReservoir.radiance, surface);

        // The Jacobian to transform a GI sample's solid angle holds the lengths and angles to the GI sample from the surfaces,
        // that are valuable information to determine if the GI sample should be combined with the current sample's stream.
        // This function also may clamp the value of the Jacobian.
        if (!RAB_ValidateGISampleWithJacobian(jacobian))
        {
            continue;
        }

        // Clamp history length
        neighborReservoir.M = min(neighborReservoir.M, stparams.maxHistoryLength);

        // Make the sample older
        ++neighborReservoir.age;

        // Valid neighbor surface and its GI reservoir. Combine the reservor.
        cachedResult |= (1u << uint(i));

        // Combine
        bool isUpdated = RTXDI_CombineGIReservoirs(curReservoir, neighborReservoir, RAB_GetNextRandom(rng), targetPdf * jacobian);
        if (isUpdated)
        {
            selected = i;
            selectedTargetPdf = targetPdf;
        }
    }

#if RTXDI_GI_ALLOWED_BIAS_CORRECTION >= RTXDI_BIAS_CORRECTION_BASIC
    if (stparams.biasCorrectionMode >= RTXDI_BIAS_CORRECTION_BASIC)
    {
        // Compute the unbiased normalization factor (instead of using 1/M)
        float pi = selectedTargetPdf;
        float piSum = selectedTargetPdf * inputReservoir.M;

        // If the GI reservoir has selected other than the initial sample, the position should be come from the previous frame.
        // However, there is no idea for the previous position of the initial GI reservoir, so it just uses the current position as its previous one.
        // float3 selectedPositionInPreviousFrame = curReservoir.position;

        // We need to walk our neighbors again
        for (int i = 0; i < totalSampleCount; ++i)
        {
            // If we skipped this neighbor above, do so again.
            if ((cachedResult & (1u << uint(i))) == 0) continue;

            // Replicate the logic for sample position computation used in the loop above
            const bool isFirstTemporalSample = i == 0;
            const bool isJitteredTemporalSample = i < temporalSampleCount;
            const bool isFallbackSample = i == temporalSampleCount;

            // Get screen-space location of neighbor
            int2 idx;
            if (isFirstTemporalSample || isFallbackSample)
            {
                idx = prevPos;

                if (stparams.enablePermutationSampling || isFallbackSample)
                    RTXDI_ApplyPermutationSampling(idx, stparams.uniformRandomNumber);
            }
            else if (isJitteredTemporalSample)
            {
                idx = prevPos + RTXDI_CalculateTemporalResamplingOffset(temporalSampleStartIdx + i, temporalJitterRadius);
            }
            else
            {
                idx = prevPos + RTXDI_CalculateSpatialResamplingOffset(neighborSampleStartIdx + i, stparams.samplingRadius, params.neighborOffsetMask);
                idx = RAB_ClampSamplePositionIntoView(idx, true);
            }

            RTXDI_ActivateCheckerboardPixel(idx, true, params.activeCheckerboardField);

            // Load our neighbor's G-buffer and its GI reservoir again.
            RAB_Surface neighborSurface = RAB_GetGBufferSurface(idx, true);

            const uint2 neighborReservoirPos = RTXDI_PixelPosToReservoirPos(idx, params.activeCheckerboardField);
            RTXDI_GIReservoir neighborReservoir = RTXDI_LoadGIReservoir(reservoirParams, neighborReservoirPos, stparams.sourceBufferIndex);

            // Clamp history length
            neighborReservoir.M = min(neighborReservoir.M, stparams.maxHistoryLength);

            // Get the PDF of the sample RIS selected in the first loop, above, *at this neighbor*
            float ps = RAB_GetGISampleTargetPdfForSurface(curReservoir.position, curReservoir.radiance, neighborSurface);

            // This should be done to correct bias.
#if RTXDI_GI_ALLOWED_BIAS_CORRECTION >= RTXDI_BIAS_CORRECTION_RAY_TRACED
            if (stparams.biasCorrectionMode == RTXDI_BIAS_CORRECTION_RAY_TRACED && ps > 0)
            {
                RAB_Surface fallbackSurface;
                if (i == 0)
                    fallbackSurface = surface;
                else
                    fallbackSurface = neighborSurface;

                if (!RAB_GetTemporalConservativeVisibility(fallbackSurface, neighborSurface, curReservoir.position))
                {
                    ps = 0;
                }
            }
#endif
            // Select this sample for the (normalization) numerator if this particular neighbor pixel
            // was the one we selected via RIS in the first loop, above.
            pi = selected == i ? ps : pi;

            // Add to the sums of weights for the (normalization) denominator
            piSum += ps * neighborReservoir.M;
        }

        // "MIS-like" normalization
        // {wSum * (pi/piSum)} * 1/selectedTargetPdf
        {
            float normalizationNumerator = pi;
            float normalizationDenominator = selectedTargetPdf * piSum;
            RTXDI_FinalizeGIResampling(curReservoir, normalizationNumerator, normalizationDenominator);
        }
    }
    else
#endif
    {
        // Normalization
        // {wSum * (1/ M)} * 1/selectedTargetPdf
        {
            float normalizationNumerator = 1.0;
            float normalizationDenominator = curReservoir.M * selectedTargetPdf;
            RTXDI_FinalizeGIResampling(curReservoir, normalizationNumerator, normalizationDenominator);
        }
    }

    return curReservoir;
}

#ifdef RTXDI_ENABLE_BOILING_FILTER

// Same as RTXDI_BoilingFilter but for GI reservoirs.
void RTXDI_GIBoilingFilter(
    uint2 LocalIndex,
    float filterStrength, // (0..1]
    inout RTXDI_GIReservoir reservoir)
{
    float weight = RTXDI_Luminance(reservoir.radiance) * reservoir.weightSum;

    if (RTXDI_BoilingFilterInternal(LocalIndex, filterStrength, weight))
        reservoir = RTXDI_EmptyGIReservoir();
}

#endif // RTXDI_ENABLE_BOILING_FILTER

#endif // GI_RESAMPLING_FUNCTIONS_HLSLI

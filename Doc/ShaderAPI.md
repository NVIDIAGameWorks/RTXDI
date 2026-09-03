# RTXDI Shader API

Most of the RTXDI functionality is implemented in shaders. To use this functionality, include the appropriate header file(s) from the [`DI/`](../Libraries/Rtxdi/Include/Rtxdi/DI/) folder into your shader source code, after defining (or at least declaring) the [bridge functions](RtxdiApplicationBridge.md).

Below is the list of shader structures and functions provided by RTXDI that can be useful to applications. Some internal functions are not shown.

## GLSL Compatibility

The RTXDI header files can be compiled as either HLSL or GLSL, with HLSL being the default language. To compile them as GLSL, define the `RTXDI_GLSL` macro. The compatibility is achieved through a small number of preprocessor macros that mostly map HLSL types and functions to GLSL. These macros can be found in `RtxdiTypes.h`.


## User-Defined Macros

### `RTXDI_ALLOWED_BIAS_CORRECTION`

Define this macro to one of the `RTXDI_BIAS_CORRECTION_...` constants to limit the most complex bias correction algorithm that is included in the shaders, to reduce code bloat.

### `RTXDI_ENABLE_PRESAMPLING`

Define this macro to `0` in order to disable the pre-sampling features and required resources (i.e. the RIS buffer).

### `RTXDI_ENABLE_STORE_RESERVOIR`

Define this macro to `0` in order to remove the `RTXDI_StoreDIReservoir` function. This is useful in shaders that have read-only access to the light reservoir buffer, e.g. for debugging purposes.

### `RTXDI_LIGHT_RESERVOIR_BUFFER`

Define this macro to a resource name for the reservoir buffer, which should have HLSL type `RWStructuredBuffer<RTXDI_PackedDIReservoir>`.

### `RTXDI_NEIGHBOR_OFFSETS_BUFFER`

Define this macro to a resource name for the neighbor offset buffer, which should have HLSL type `Buffer<float2>`.

### `RTXDI_REGIR_MODE`

Define this macro to one of the `RTXDI_REGIR_DISABLED`, `RTXDI_REGIR_GRID`, `RTXDI_REGIR_ONION` to select the version of the ReGIR spatial structure to implement, if any.

### `RTXDI_RIS_BUFFER`

Define this macro to a resource name for the RIS buffer, which should have HLSL type `RWBuffer<uint2>`. Not necessary when `RTXDI_ENABLE_PRESAMPLING` is `0`.


## Structures

### `RTXDI_PackedDIReservoir`

A compact representation of a single light reservoir that should be stored in a structured buffer.

### `RTXDI_DIReservoir`

This structure represents a single light reservoir that stores the weights, the sample ref, sample count (M), and visibility for reuse. It can be serialized into `RTXDI_PackedDIReservoir` for storage using the `RTXDI_PackDIReservoir` function, and deserialized from that representation using the `RTXDI_UnpackDIReservoir` function.

### `RTXDI_DIInitialSamplingParameters`

Defined in [`DI/ReSTIRDIParameters.h`](../Libraries/Rtxdi/Include/Rtxdi/DI/ReSTIRDIParameters.h). Describes sample counts per strategy (`numLocalLightSamples`, `numInfiniteLightSamples`, `numEnvironmentSamples`, `numBrdfSamples`), BRDF sampling limits (`brdfCutoff`, `brdfRayMinT`), `localLightSamplingMode`, and related flags.

### `RTXDI_RuntimeParameters`

Defined in [`RtxdiParameters.h`](../Libraries/Rtxdi/Include/Rtxdi/RtxdiParameters.h). Specifies global per-frame RTXDI options such as Checkerboard and frame parameters (`neighborOffsetMask`, `activeCheckerboardField`, `frameIndex`).

### `RTXDI_ReservoirBufferParameters`

Reservoir buffer layout (`reservoirBlockRowPitch`, `reservoirArrayPitch`), used to load/store reservoirs from the reservoir buffer.

### `RTXDI_LightBufferParameters`

Groups local lights, infinite lights, and environment light parameters for initial sampling (`localLightBufferRegion`, `infiniteLightBufferRegion`, `environmentLightParams`).

### `RTXDI_InitialSamplingMisData` and `RTXDI_ComputeInitialSamplingMisData`

`RTXDI_InitialSamplingMisData` holds MIS weights used during initial sampling. Compute it with `RTXDI_ComputeInitialSamplingMisData(RTXDI_DIInitialSamplingParameters)` before calling functions that take a `misData` argument (see [`DI/InitialSampling.hlsli`](../Libraries/Rtxdi/Include/Rtxdi/DI/InitialSampling.hlsli)).

### Resampling parameter structs

`RTXDI_DITemporalResamplingParameters`, `RTXDI_DISpatialResamplingParameters`, and `RTXDI_DISpatioTemporalResamplingParameters` are defined in [`DI/ReSTIRDIParameters.h`](../Libraries/Rtxdi/Include/Rtxdi/DI/ReSTIRDIParameters.h). Prefer that header for authoritative member lists; the excerpts below only document entry-point parameter ordering.


## Reservoir Functions

### `RTXDI_EmptyDIReservoir`

    RTXDI_DIReservoir RTXDI_EmptyDIReservoir()

Returns an empty reservoir object.

### `RTXDI_IsValidDIReservoir`

    bool RTXDI_IsValidDIReservoir(const RTXDI_DIReservoir reservoir)

Returns `true` if the provided reservoir contains a valid light sample.

### `RTXDI_GetDIReservoirLightIndex`

    uint RTXDI_GetDIReservoirLightIndex(const RTXDI_DIReservoir reservoir)

Returns the light index stored in the reservoir. For empty reservoirs it will return 0, which could be a valid light index, so a call to `RTXDI_IsValidDIReservoir` is necessary to determine if the reservoir is empty or not.

### `RTXDI_GetDIReservoirSampleUV`

    float2 RTXDI_GetDIReservoirSampleUV(const RTXDI_DIReservoir reservoir)

Returns the sample UV stored in the reservoir.

### `RTXDI_GetDIReservoirInvPdf`

    float RTXDI_GetDIReservoirInvPdf(const RTXDI_DIReservoir reservoir)

Returns the inverse PDF of the reservoir. This value should be used to scale the results of surface shading using the reservoir.

### `RTXDI_LoadDIReservoir`

    RTXDI_DIReservoir RTXDI_LoadDIReservoir(
        RTXDI_ReservoirBufferParameters reservoirParams,
        uint2 reservoirPosition,
        uint reservoirArrayIndex)

Loads and unpacks a reservoir from the provided reservoir storage buffer. The buffer normally contains multiple 2D arrays of reservoirs, corresponding to screen pixels, so the function takes the reservoir position and array index and translates those to the buffer index. 

### `RTXDI_StoreDIReservoir`

    void RTXDI_StoreDIReservoir(
        const RTXDI_DIReservoir reservoir,
        RTXDI_ReservoirBufferParameters reservoirParams,
        uint2 reservoirPosition,
        uint reservoirArrayIndex)

Packs and stores the reservoir into the provided reservoir storage buffer. Buffer addressing works similar to `RTXDI_LoadDIReservoir`.

### `RTXDI_StoreVisibilityInDIReservoir`

    void RTXDI_StoreVisibilityInDIReservoir(
        inout RTXDI_DIReservoir reservoir,
        float3 visibility,
        bool discardIfInvisible)

Stores the visibility term in a compressed form in the reservoir. This function should be called when a shadow ray is cast between a surface and a light sample in the initial or final shading passes. The `discardIfInvisible` parameter controls whether the reservoir should be reset to an invalid state if the visibility is zero, which reduces noise; it's safe to use that for the initial samples, but discarding samples when their final visibility is zero may result in darkening bias.

### `RTXDI_GetDIReservoirVisibility`

    struct RTXDI_VisibilityReuseParameters
    {
        uint maxAge;
        float maxDistance;
    };
    bool RTXDI_GetDIReservoirVisibility(
        const RTXDI_DIReservoir reservoir,
        const RTXDI_VisibilityReuseParameters params,
        out float3 o_visibility)

Loads the visibility term from the reservoir, if it is applicable in the reservoir's current location. The applicability is determined by comparing the distance and age of the stored visibility term with the provided thresholds. When the visibility can be used, this function returns `true`, and the application may use this stored visibility instead of tracing a final visibility ray.

Using higher threshold values for distance and age result in a higher degree of visibility reuse, which improves performance because fewer rays are needed, but also increase bias. On the other hand, this bias brightens the areas around shadow edges, while the other bias that comes from spatial and temporal reuse without ray traced bias correction darkens the same areas, so these two biases partially cancel out.


## Basic Resampling Functions

### `RTXDI_StreamSample`

    bool RTXDI_StreamSample(
        inout RTXDI_DIReservoir reservoir,
        uint lightIndex,
        float2 uv,
        float random,
        float targetPdf,
        float invSourcePdf)

Adds one light sample to the reservoir. Returns `true` if the sample was selected for the reservoir, `false` if not.

This function implements Algorithm (3) from the ReSTIR paper, "Streaming RIS using weighted reservoir sampling".

### `RTXDI_CombineDIReservoirs`

    bool RTXDI_CombineDIReservoirs(
        inout RTXDI_DIReservoir reservoir,
        const RTXDI_DIReservoir newReservoir,
        float random,
        float targetPdf)

Adds a reservoir with one sample into this reservoir. Returns `true` if the new reservoir's sample was selected, `false` if not. The new reservoir's `targetPdf` field is ignored and replaced with the `targetPdf` parameter of the function.

This function implements Algorithm (4) from the ReSTIR paper, "Combining the streams of multiple reservoirs".

### `RTXDI_FinalizeResampling`

    void RTXDI_FinalizeResampling(
        inout RTXDI_DIReservoir reservoir,
        float normalizationNumerator,
        float normalizationDenominator)

Performs normalization of the reservoir after streaming. After this function is called, the reservoir's `weightSum` field becomes its inverse PDF that can be used for shading or for further reservoir combinations.

The `normalizationNumerator` and `normalizationDenominator` parameters specify the normalization scale for bias correction. Basic applications like streaming of initial light samples will set the numerator to 1.0 and the denominator to M (the number of samples in the reservoir). Spatiotemporal resampling will normally compute the numerator and denominator by weighing the final selected sample against the original surfaces used in resampling.

This function implements Equation (6) from the ReSTIR paper.

### `RTXDI_InternalSimpleResample`
```
bool RTXDI_InternalSimpleResample(
    inout RTXDI_DIReservoir reservoir,
    const RTXDI_DIReservoir newReservoir,
    float random,
    float targetPdf = 1.0,
    float sampleNormalization = 1.0,
    float sampleM = 1.0)
```

Adds `newReservoir` into `reservoir`, returns true if the new reservoir's sample was selected.
This is a very general form, allowing input parameters to specify normalization and `targetPdf`
rather than computing them from `newReservoir`.


## Low-Level Sampling Functions

Presampling helpers (`RTXDI_PresampleLocalLights`, `RTXDI_PresampleEnvironmentMap`, and ReGIR variants) require `RTXDI_ENABLE_PRESAMPLING` and `RTXDI_RIS_BUFFER`. Source: [`LightSampling/PresamplingFunctions.hlsli`](../Libraries/Rtxdi/Include/Rtxdi/LightSampling/PresamplingFunctions.hlsli).

### `RTXDI_SamplePdfMipmap`

    void RTXDI_SamplePdfMipmap(
        inout RTXDI_RandomSamplerState rng,
        RTXDI_TEX2D pdfTexture,
        uint2 pdfTextureSize,
        out uint2 position,
        out float pdf)

Performs importance sampling of a set of items with their PDF values stored in a 2D texture mipmap. The texture must have power-of-2 dimensions and a mip chain up to 2x2 pixels (or 2x1 or 1x2 if the texture is rectangular). The mip chain must be generated using a regular 2x2 box filter, which means any standard way of generating a mipmap should work.

The function returns the position of the final selected texel in the `position` parameter, and its normalized selection PDF in the `pdf` parameter. If the PDF texture is empty or malformed (i.e. has four adjacent zeros in one mip level and a nonzero corresponding texel in the next mip level), the returned PDF will be zero.

### `RTXDI_PresampleLocalLights`

    void RTXDI_PresampleLocalLights(
        inout RTXDI_RandomSamplerState rng,
        RTXDI_TEX2D pdfTexture,
        uint2 pdfTextureSize,
        uint tileIndex,
        uint sampleInTile,
        RTXDI_LightBufferRegion localLightBufferRegion,
        RTXDI_RISBufferSegmentParameters localLightsRISBufferSegmentParams)

Selects one local light using the provided PDF texture and stores its information in the RIS buffer at the position identified by the `tileIndex` and `sampleInTile` parameters. Additionally, stores compact light information in the companion buffer that is managed by the application, through the `RAB_StoreCompactLightInfo` function.

### `RTXDI_PresampleEnvironmentMap`

    void RTXDI_PresampleEnvironmentMap(
        inout RTXDI_RandomSamplerState rng,
        RTXDI_TEX2D pdfTexture,
        uint2 pdfTextureSize,
        uint tileIndex,
        uint sampleInTile,
        RTXDI_RISBufferSegmentParameters risBufferSegmentParams)

Selects one environment map texel using the provided PDF texture and stores its information in the RIS buffer at the position identified by the `tileIndex` and `sampleInTile` parameters.

### `RTXDI_PresampleLocalLightsForReGIR`

    void RTXDI_PresampleLocalLightsForReGIR(
        inout RTXDI_RandomSamplerState rng,
        inout RTXDI_RandomSamplerState coherentRng,
        uint lightSlot,
        RTXDI_LightBufferRegion localLightBufferRegion,
        RTXDI_RISBufferSegmentParameters localLightRISBufferSegmentParams,
        ReGIR_Parameters regirParams)

Selects one local light using RIS with `regirParams.commonParams.numRegirBuildSamples` proposals weighted relative to a specific ReGIR world space cell. The cell position and size are derived from its index; the cell index is derived from the `lightSlot` parameter: each cell contains a number of light slots packed together and stored in the RIS buffer. Additionally, stores compact light information in the companion buffer that is managed by the application, through the `RAB_StoreCompactLightInfo` function.

The weights of lights relative to ReGIR cells are computed using the [`RAB_GetLightTargetPdfForVolume`](RtxdiApplicationBridge.md#rab_getlighttargetpdfforvolume) application-defined function.

### `RTXDI_SampleLocalLights`

    RTXDI_DIReservoir RTXDI_SampleLocalLights(
        inout RTXDI_RandomSamplerState rng,
        inout RTXDI_RandomSamplerState coherentRng,
        RAB_Surface surface,
        RTXDI_DIInitialSamplingParameters sampleParams,
        RTXDI_InitialSamplingMisData misData,
        ReSTIRDI_LocalLightSamplingMode localLightSamplingMode,
        RTXDI_LightBufferRegion localLightBufferRegion,
    #if RTXDI_ENABLE_PRESAMPLING
        RTXDI_RISBufferSegmentParameters localLightRISBufferSegmentParams,
    #if RTXDI_REGIR_MODE != RTXDI_REGIR_DISABLED
        ReGIR_Parameters regirParams,
    #endif
    #endif
        out RAB_LightSample o_selectedSample)

Selects one local light sample using RIS with `sampleParams.numLocalLightSamples` proposals weighted relative to the provided `surface`, and returns a reservoir with the selected light sample. The sample itself is returned in the `o_selectedSample` parameter.

The proposals are picked from a RIS buffer tile that's picked using `coherentRng`, which should generate the same random numbers for a group of adjacent shader threads for performance. If the RIS buffer is not available, this function will fall back to uniform sampling from the local light pool, which is typically much more noisy. The RIS buffer must be pre-filled with samples using the [`RTXDI_PresampleLocalLights`](#rtxdi_presamplelocallights) function in a preceding pass.

When `localLightSamplingMode` is `ReSTIRDI_LocalLightSamplingMode_REGIR_RIS` and ReGIR is enabled (`RTXDI_REGIR_MODE != RTXDI_REGIR_DISABLED`), sampling uses ReGIR cells at the surface position (with jitter from `regirParams`); otherwise behavior follows power RIS or uniform mode. Pass `regirParams` only when that preprocessor branch is active.

### `RTXDI_SampleInfiniteLights`

    RTXDI_DIReservoir RTXDI_SampleInfiniteLights(
        inout RTXDI_RandomSamplerState rng,
        RAB_Surface surface,
        uint numSamples,
        RTXDI_LightBufferRegion infiniteLightBufferRegion,
        inout RAB_LightSample o_selectedSample)

Selects one infinite light sample using RIS with `numSamples` proposals weighted relative to the provided `surface`, and returns a reservoir with the selected light sample. The sample itself is returned in the `o_selectedSample` parameter.

### `RTXDI_SampleEnvironmentMap`

Available when `RTXDI_ENABLE_PRESAMPLING` is non-zero.

    RTXDI_DIReservoir RTXDI_SampleEnvironmentMap(
        inout RTXDI_RandomSamplerState rng,
        inout RTXDI_RandomSamplerState coherentRng,
        RAB_Surface surface,
        RTXDI_DIInitialSamplingParameters sampleParams,
        RTXDI_InitialSamplingMisData misData,
        RTXDI_EnvironmentLightBufferParameters params,
        RTXDI_RISBufferSegmentParameters risBufferSegmentParams,
        out RAB_LightSample o_selectedSample)

Selects one sample from the importance sampled environment light using RIS with `sampleParams.numEnvironmentSamples` proposals weighted relative to the provided `surface`, and returns a reservoir with the selected light sample. The sample itself is returned in the `o_selectedSample` parameter.

The proposals are picked from a RIS buffer tile, similar to [`RTXDI_SampleLocalLights`](#rtxdi_samplelocallights). The RIS buffer must be pre-filled with samples using the [`RTXDI_PresampleEnvironmentMap`](#rtxdi_presampleenvironmentmap) function in a preceding pass.

### `RTXDI_SampleBrdf`

```
    RTXDI_DIReservoir RTXDI_SampleBrdf(
        inout RTXDI_RandomSamplerState rng,
        RAB_Surface surface,
        uint numBrdfSamples,
        float brdfCutoff,
        float brdfRayMinT,
        RTXDI_InitialSamplingMisData misData,
        inout RTXDI_RandomSamplerState coherentRng,
        RTXDI_LightBufferParameters lightBufferParams,
        out RAB_LightSample o_selectedSample)
```

Selects one local light or environment map sample using RIS with `numBrdfSamples` BRDF ray traces into the scene, weights the proposals relative to provided `surface`, and returns a reservoir with the selected light sample. The sample itself is returned in the `o_selectedSample` parameter.

Depending on the application provided ray trace function, if a local light is hit, a local light proposal is generated, if the ray trace result returns `false`, an environment map proposal is generated. Each proposal is multi-importance weighted between its RIS selection probability relative to solid angle, and the BRDF's direction probability relative to solid angle. 


### `RTXDI_StreamNeighborWithPairwiseMIS`

```
    bool RTXDI_StreamNeighborWithPairwiseMIS(inout RTXDI_DIReservoir reservoir,
        float random,
        const RTXDI_DIReservoir neighborReservoir,
        const RAB_Surface neighborSurface,
        const RTXDI_DIReservoir canonicalReservoir,
        const RAB_Surface canonicalSurface,
        const uint numberOfNeighborsInStream)
```

"Pairwise MIS" is a MIS approach that is O(N) instead of O(N^2) for N estimators.  The idea is you know
a canonical sample which is a known (pretty-)good estimator, but you'd still like to improve the result
given multiple other candidate estimators.  You can do this in a pairwise fashion, MIS'ing between each
candidate and the canonical sample. `RTXDI_StreamNeighborWithPairwiseMIS()` is executed once for each 
candidate, after which the MIS is completed by calling `RTXDI_StreamCanonicalWithPairwiseStep()` once for
the canonical sample.

See Chapter 9.1 of https://digitalcommons.dartmouth.edu/dissertations/77/, especially Eq. 9.10 & Alg. 8

### `RTXDI_StreamCanonicalWithPairwiseStep`

```
    bool RTXDI_StreamCanonicalWithPairwiseStep(inout RTXDI_DIReservoir reservoir,
        float random,
        const RTXDI_DIReservoir canonicalReservoir,
        const RAB_Surface canonicalSurface)
```

Called to finish the process of doing pairwise MIS.  This function must be called after all required calls to
`RTXDI_StreamNeighborWithPairwiseMIS()`, since pairwise MIS overweighs the canonical sample.  This function 
compensates for this overweighting, but it can only happen after all neighbors have been processed.


## High-Level Sampling and Resampling Functions

### `RTXDI_SampleLightsForSurface`

```
    RTXDI_DIReservoir RTXDI_SampleLightsForSurface(
        inout RTXDI_RandomSamplerState rng,
        inout RTXDI_RandomSamplerState coherentRng,
        RAB_Surface surface,
        RTXDI_DIInitialSamplingParameters sampleParams,
        RTXDI_LightBufferParameters lightBufferParams,
    #if RTXDI_ENABLE_PRESAMPLING
        RTXDI_RISBufferSegmentParameters localLightRISBufferSegmentParams,
        RTXDI_RISBufferSegmentParameters environmentLightRISBufferSegmentParams,
    #if RTXDI_REGIR_MODE != RTXDI_REGIR_DISABLED
        ReGIR_Parameters regirParams,
    #endif
    #endif
        out RAB_LightSample o_lightSample)
```

This function is a combination of `RTXDI_SampleLocalLights` (which can use ReGIR when enabled), `RTXDI_SampleInfiniteLights`, `RTXDI_SampleEnvironmentMap` (when presampling is enabled), and `RTXDI_SampleBrdf`. Reservoirs returned from each function are combined into one final reservoir, which is returned.

### `RTXDI_DITemporalResampling`

    RTXDI_DIReservoir RTXDI_DITemporalResampling(
        uint2 pixelPosition,
        RAB_Surface surface,
        RTXDI_DIReservoir curSample,
        inout RTXDI_RandomSamplerState rng,
        RTXDI_RuntimeParameters rParams,
        RTXDI_ReservoirBufferParameters reservoirParams,
        float3 screenSpaceMotion,
        uint sourceBufferIndex,
        RTXDI_DITemporalResamplingParameters tparams,
        out int2 temporalSamplePixelPos,
        inout RAB_LightSample selectedLightSample)

Implements the core functionality of the temporal resampling pass. Takes the previous G-buffer, motion vectors, and two light reservoir buffers - current and previous - as inputs. Tries to match the surfaces in the current frame to surfaces in the previous frame. If a match is found for a given pixel, the current and previous reservoirs are combined.

An optional visibility ray may be cast if enabled with the `tparams.biasCorrectionMode` setting, to reduce the resampling bias. That visibility ray should ideally be traced through the previous frame BVH, but can also use the current frame BVH if the previous is not available - that will produce more bias.

Temporal reuse parameters are passed in `tparams` (`RTXDI_DITemporalResamplingParameters`). Motion and buffer selection use `screenSpaceMotion` and `sourceBufferIndex` as separate arguments.

### `RTXDI_DISpatialResampling`

    RTXDI_DIReservoir RTXDI_DISpatialResampling(
        uint2 pixelPosition,
        RAB_Surface centerSurface,
        RTXDI_DIReservoir centerSample,
        inout RTXDI_RandomSamplerState rng,
        RTXDI_RuntimeParameters rParams,
        RTXDI_ReservoirBufferParameters reservoirParams,
        uint sourceBufferIndex,
        RTXDI_DISpatialResamplingParameters sparams,
        inout RAB_LightSample selectedLightSample)


Implements the core functionality of the spatial resampling pass. Operates on the current frame G-buffer and its reservoirs. For each pixel, considers a number of its neighbors and, if their surfaces are similar enough to the current pixel, combines their light reservoirs.

Optionally, one visibility ray is traced for each neighbor being considered, to reduce bias, if enabled with the `sparams.biasCorrectionMode` setting.

Spatial reuse parameters are passed in `sparams` (`RTXDI_DISpatialResamplingParameters`). The reservoir buffer index is `sourceBufferIndex`.

### `RTXDI_DISpatioTemporalResampling`

    RTXDI_DIReservoir RTXDI_DISpatioTemporalResampling(
        uint2 pixelPosition,
        RAB_Surface surface,
        RTXDI_DIReservoir curSample,
        inout RTXDI_RandomSamplerState rng,
        float3 screenSpaceMotion,
        uint sourceBufferIndex,
        RTXDI_RuntimeParameters rParams,
        RTXDI_ReservoirBufferParameters reservoirParams,
        RTXDI_DISpatioTemporalResamplingParameters stparams,
        out int2 temporalSamplePixelPos,
        inout RAB_LightSample selectedLightSample)

Implements the core functionality of a combined spatiotemporal resampling pass. This is similar to a sequence of `RTXDI_DITemporalResampling` and `RTXDI_DISpatialResampling`, with the exception that the input reservoirs are all taken from the previous frame. This function is useful for implementing a lighting solution in a single shader, which generates the initial samples, applies spatiotemporal resampling, and shades the final samples.

### `RTXDI_BoilingFilter`

Compiled only when `RTXDI_ENABLE_BOILING_FILTER` is defined.

    void RTXDI_BoilingFilter(
        uint2 LocalIndex,
        float filterStrength,
        inout RTXDI_DIReservoir reservoir)

Applies a boiling filter over all threads in the compute shader thread group. This filter attempts to reduce boiling by removing reservoirs whose weight is significantly higher than the weights of their neighbors. Essentially, when some lights are important for a surface but they are also unlikely to be located in the initial sampling pass, ReSTIR will try to hold on to these lights by spreading them around, and if such important lights are sufficiently rare, the result will look like light bubbles appearing and growing, then fading. This filter attempts to detect and remove such rare lights, trading boiling for bias.


## Utility Functions

### Random sampler state

Library entry points take `inout RTXDI_RandomSamplerState` for RNG state.

### `RTXDI_ComputeInitialSamplingMisData`

```
RTXDI_InitialSamplingMisData RTXDI_ComputeInitialSamplingMisData(
    RTXDI_DIInitialSamplingParameters initialSamplingParams)
```

Computes MIS weights for `RTXDI_InitialSamplingMisData`. Call this after filling `RTXDI_DIInitialSamplingParameters`, then pass the result to initial sampling functions that require `misData`.

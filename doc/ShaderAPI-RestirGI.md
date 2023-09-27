# RTXDI Shader API for ReSTIR GI

This document lists the macros, structures and functions that are specific to the ReSTIR GI subsystem of RTXDI.

## User-Defined Macros

### `RTXDI_WITH_RESTIR_GI`

When defined, includes the [ReSTIR GI](RestirGI.md) implementation, with all of its functions and structures - both on the shader side and the CPU side.

### `RTXDI_ENABLE_STORE_RESERVOIR`

Define this macro to `0` in order to remove the `RTXDI_GI_StoreReservoir` function. This is useful in shaders that have read-only access to the light reservoir buffer, e.g. for debugging purposes.

### `RTXDI_GI_ALLOWED_BIAS_CORRECTION`

Define this macro to one of the `RTXDI_BIAS_CORRECTION_...` constants to limit the most complex bias correction algorithm that is included in the shaders, to reduce code bloat.

### `RTXDI_GI_RESERVOIR_BUFFER`

Define this macro to a resource name for the ReSTIR GI reservoir buffer, which should have HLSL type `RWStructuredBuffer<RTXDI_PackedGIReservoir>`.

### `RTXDI_NEIGHBOR_OFFSETS_BUFFER`

Define this macro to a resource name for the neighbor offset buffer, which should have HLSL type `Buffer<float2>`.


## Structures

### `RTXDI_ResamplingRuntimeParameters`

See [Shader API](ShaderAPI.md)

### `RTXDI_PackedGIReservoir`

A compact representation of a single GI reservoir that should be stored in a structured buffer.

### `RTXDI_GIReservoir`

This structure represents a single surface reservoir that stores the surface position, orientation, radiance, and statistical parameters. It can be serialized into `RTXDI_PackedGIReservoir` for storage using the `RTXDI_PackGIReservoir` function, and deserialized from that representation using the `RTXDI_UnpackGIReservoir` function.


## Reservoir Functions

### `RTXDI_EmptyGIReservoir`

    RTXDI_GIReservoir RTXDI_EmptyGIReservoir()

Returns an empty reservoir object.

### `RTXDI_IsValidReservoir`

    bool RTXDI_IsValidGIReservoir(const RTXDI_GIReservoir reservoir)

Returns `true` if the provided reservoir contains a valid GI sample.

### `RTXDI_LoadGIReservoir`

    RTXDI_GIReservoir RTXDI_LoadGIReservoir(
        RTXDI_ResamplingRuntimeParameters params,
        uint2 reservoirPosition,
        uint reservoirArrayIndex)

    RTXDI_GIReservoir RTXDI_LoadGIReservoir(
        RTXDI_ResamplingRuntimeParameters params,
        uint2 reservoirPosition,
        uint reservoirArrayIndex,
        out uint miscFlags)

Loads and unpacks a GI reservoir from the provided reservoir storage buffer. The buffer normally contains multiple 2D arrays of reservoirs, corresponding to screen pixels, so the function takes the reservoir position and array index and translates those to the buffer index. The optional `miscFlags` output parameter returns a custom 16-bit field that applications can use for their needs.

### `RTXDI_StoreGIReservoir`

    void RTXDI_StoreGIReservoir(
        const RTXDI_GIReservoir reservoir,
        RTXDI_ResamplingRuntimeParameters params,
        uint2 reservoirPosition,
        uint reservoirArrayIndex)

    void RTXDI_StoreGIReservoir(
        const RTXDI_GIReservoir reservoir,
        const uint miscFlags,
        RTXDI_ResamplingRuntimeParameters params,
        uint2 reservoirPosition,
        uint reservoirArrayIndex)

Packs and stores the GI reservoir into the provided reservoir storage buffer. Buffer addressing works similar to `RTXDI_LoadGIReservoir`.


## Basic Resampling Functions

### `RTXDI_CombineGIReservoirs`

    bool RTXDI_CombineGIReservoirs(
        inout RTXDI_GIReservoir reservoir,
        const RTXDI_GIReservoir newReservoir,
        float random,
        float targetPdf)

Adds a reservoir with one sample into this reservoir. Returns `true` if the new reservoir's sample was selected, `false` if not.

### `RTXDI_FinalizeGIResampling`

    void RTXDI_FinalizeGIResampling(
        inout RTXDI_GIReservoir reservoir,
        float normalizationNumerator,
        float normalizationDenominator)

Performs normalization of the reservoir after streaming. After this function is called, the reservoir's `weightSum` field becomes its inverse PDF that can be used for shading or for further reservoir combinations.

The `normalizationNumerator` and `normalizationDenominator` parameters specify the normalization scale for bias correction. Basic applications like streaming of initial light samples will set the numerator to 1.0 and the denominator to M (the number of samples in the reservoir). Spatiotemporal resampling will normally compute the numerator and denominator by weighing the final selected sample against the original surfaces used in resampling.

**Note:** unlike with direct lighting reservoirs, the GI reservoirs do not store the target PDF. In order for `RTXDI_FinalizeGIResampling`  to work correctly, the denominator must include the target PDF of the selected sample!

### `RTXDI_MakeGIReservoir`

    RTXDI_GIReservoir RTXDI_MakeGIReservoir(
        const float3 samplePos,
        const float3 sampleNormal,
        const float3 sampleRadiance,
        const float samplePdf)

Creates a GI reservoir from a raw light sample.

**Note**: the original sample PDF can be embedded into `sampleRadiance`, in which case the `samplePdf` parameter should be set to 1.0.


## High-Level Sampling and Resampling Functions

### `RTXDI_GITemporalResampling`

    struct RTXDI_GITemporalResamplingParameters
    {
        float3 screenSpaceMotion;
        uint sourceBufferIndex;
        uint maxHistoryLength;
        uint biasCorrectionMode;
        float depthThreshold;
        float normalThreshold;
        uint maxReservoirAge;
        bool enablePermutationSampling;
        bool enableFallbackSampling;
    };
    GI_GIReservoir RTXDI_GITemporalResampling(
        const uint2 pixelPosition,
        const RAB_Surface surface,
        const RTXDI_GIReservoir inputReservoir,
        inout RAB_RandomSamplerState rng,
        const RTXDI_GITemporalResamplingParameters tparams,
        const RTXDI_ResamplingRuntimeParameters params)

Implements the core functionality of the temporal resampling pass. Takes the previous G-buffer, motion vectors, and two GI reservoir buffers - current and previous - as inputs. Tries to match the surfaces in the current frame to surfaces in the previous frame. If a match is found for a given pixel, the current and previous reservoirs are combined.

An optional visibility ray may be cast if enabled with the `tparams.biasCorrectionMode` setting, to reduce the resampling bias. That visibility ray should ideally be traced through the previous frame BVH, but can also use the current frame BVH if the previous is not available - that will produce more bias.

For more information on the members of the `RTXDI_GITemporalResamplingParameters` structure, see the comments in the source code.

### `RTXDI_SpatialResampling`

    struct RTXDI_GISpatialResamplingParameters
    {
        uint sourceBufferIndex;
        float depthThreshold;
        float normalThreshold;
        uint numSamples;
        float samplingRadius;
        uint biasCorrectionMode;
    };
    RTXDI_GIReservoir RTXDI_GISpatialResampling(
        const uint2 pixelPosition,
        const RAB_Surface surface,
        const RTXDI_GIReservoir inputReservoir,
        inout RAB_RandomSamplerState rng,
        const RTXDI_GISpatialResamplingParameters sparams,
        const RTXDI_ResamplingRuntimeParameters params)

Implements the core functionality of the spatial resampling pass. Operates on the current frame G-buffer and its reservoirs. For each pixel, considers a number of its neighbors and, if their surfaces are similar enough to the current pixel, combines their reservoirs.

Optionally, one visibility ray is traced for each neighbor being considered, to reduce bias, if enabled with the `sparams.biasCorrectionMode` setting.

For more information on the members of the `RTXDI_GISpatialResamplingParameters` structure, see the comments in the source code.


### `RTXDI_SpatioTemporalResampling`


    struct RTXDI_GISpatioTemporalResamplingParameters
    {
        float3 screenSpaceMotion;
        uint sourceBufferIndex;
        uint maxHistoryLength;
        float depthThreshold;
        float normalThreshold;
        uint maxReservoirAge;
        uint numSpatialSamples;
        float samplingRadius;
        uint biasCorrectionMode;
        bool enablePermutationSampling;
        bool enableFallbackSampling;
    };
    RTXDI_GIReservoir RTXDI_GISpatioTemporalResampling(
        const uint2 pixelPosition,
        const RAB_Surface surface,
        RTXDI_GIReservoir inputReservoir,
        inout RAB_RandomSamplerState rng,
        const RTXDI_GISpatioTemporalResamplingParameters stparams,
        const RTXDI_ResamplingRuntimeParameters params)

Implements the core functionality of a combined spatiotemporal resampling pass. This is similar to a sequence of `RTXDI_GITemporalResampling` and `RTXDI_GISpatialResampling`, with the exception that the input reservoirs are all taken from the previous frame. This function is useful for implementing a lighting solution in a single shader, which generates the initial samples, applies spatiotemporal resampling, and shades the final samples.

### `RTXDI_GIBoilingFilter`

    void RTXDI_GIBoilingFilter(
        uint2 LocalIndex,
        float filterStrength,
        RTXDI_ResamplingRuntimeParameters params,
        inout RTXDI_GIReservoir reservoir)

Applies a boiling filter over all threads in the compute shader thread group. This filter attempts to reduce boiling by removing reservoirs whose weighted radiance is significantly higher than the weighted radiances of their neighbors.


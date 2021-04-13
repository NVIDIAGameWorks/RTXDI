# RTXDI Shader API

Most of the RTXDI functionality is implemented in shaders. To use this functionality, include the [`ResamplingFunctions.hlsli`](../rtxdi-sdk/include/rtxdi/ResamplingFunctions.hlsli) file into your shader source code, after defining (or at least declaring) the [bridge functions](RtxdiApplicationBridge.md).

Below is the list of shader structures and functions provided by RTXDI that can be useful to applications. Some internal functions are not shown.


## Structures

### `RTXDI_ResamplingRuntimeParameters`

This structure contains runtime parameters that are accepted by most functions. It can be passed from the CPU side directly through a constant buffer. It is declared in [`RtxdiParameters.h`](../rtxdi-sdk/include/rtxdi/RtxdiParameters.h), which can be included into shader code as well as host-side C++ code. To fill an instance of `RTXDI_ResamplingRuntimeParameters` with valid data, call the `rtxdi::Context::FillRuntimeParameters(...)` function on every frame.

### `RTXDI_PackedReservoir`

A compact representation of a single light reservoir that should be stored in a structured buffer.

### `RTXDI_Reservoir`

This structure represents a single light reservoir that stores the weights, the sample ref, sample count (M), and visibility for reuse. It can be serialized into `RTXDI_PackedReservoir` for storage using the `RTXDI_PackReservoir` function, and deserialized from that representation using the `RTXDI_UnpackReservoir` function.


## Reservoir Functions

### `RTXDI_EmptyReservoir`

    RTXDI_Reservoir RTXDI_EmptyReservoir()

Returns an empty reservoir object.

### `RTXDI_IsValidReservoir`

    bool RTXDI_IsValidReservoir(const RTXDI_Reservoir reservoir)

Returns `true` if the provided reservoir contains a valid light sample.

### `RTXDI_GetReservoirLightIndex`

    uint RTXDI_GetReservoirLightIndex(const RTXDI_Reservoir reservoir)

Returns the light index stored in the reservoir. For empty reservoirs it will return 0, which could be a valid light index, so a call to `RTXDI_IsValidReservoir` is necessary to determine if the reservoir is empty or not.

### `RTXDI_GetReservoirSampleUV`

    float2 RTXDI_GetReservoirSampleUV(const RTXDI_Reservoir reservoir)

Returns the sample UV stored in the reservoir.

### `RTXDI_GetReservoirInvPdf`

    float RTXDI_GetReservoirInvPdf(const RTXDI_Reservoir reservoir)

Returns the inverse PDF of the reservoir. This value should be used to scale the results of surface shading using the reservoir.

### `RTXDI_LoadReservoir`

    RTXDI_Reservoir RTXDI_LoadReservoir(
        RTXDI_ResamplingRuntimeParameters params,
        RWStructuredBuffer<RTXDI_PackedReservoir> LightReservoirs,
        uint2 reservoirPosition,
        uint reservoirArrayIndex)

Loads and unpacks a reservoir from the provided reservoir storage buffer. The buffer normally contains multiple 2D arrays of reservoirs, corresponding to screen pixels, so the function takes the reservoir position and array index and translates those to the buffer index. 

### `RTXDI_StoreReservoir`

    void RTXDI_StoreReservoir(
        const RTXDI_Reservoir reservoir,
        RTXDI_ResamplingRuntimeParameters params,
        RWStructuredBuffer<RTXDI_PackedReservoir> LightReservoirs,
        uint2 reservoirPosition,
        uint reservoirArrayIndex)

Packs and stores the reservoir into the provided reservoir storage buffer. Buffer addressing works similar to `RTXDI_LoadReservoir`.

### `RTXDI_StoreVisibilityInReservoir`

    void RTXDI_StoreVisibilityInReservoir(
        inout RTXDI_Reservoir reservoir,
        float3 visibility,
        bool discardIfInvisible)

Stores the visibility term in a compressed form in the reservoir. This function should be called when a shadow ray is cast between a surface and a light sample in the initial or final shading passes. The `discardIfInvisible` parameter controls whether the reservoir should be reset to an invalid state if the visibility is zero, which reduces noise; it's safe to use that for the initial samples, but discarding samples when their final visibility is zero may result in darkening bias.

### `RTXDI_GetReservoirVisibility`

    struct RTXDI_VisibilityReuseParameters
    {
        uint maxAge;
        float maxDistance;
    };
    bool RTXDI_GetReservoirVisibility(
        const RTXDI_Reservoir reservoir,
        const RTXDI_VisibilityReuseParameters params,
        out float3 o_visibility)

Loads the visibility term from the reservoir, if it is applicable in the reservoir's current location. The applicability is determined by comparing the distance and age of the stored visibility term with the provided thresholds. When the visibility can be used, this function returns `true`, and the application may use this stored visibility instead of tracing a final visibility ray.

Using higher threshold values for distance and age result in a higher degree of visibility reuse, which improves performance because fewer rays are needed, but also increase bias. On the other hand, this bias brightens the areas around shadow edges, while the other bias that comes from spatial and temporal reuse without ray traced bias correction darkens the same areas, so these two biases partially cancel out.


## Basic Resampling Functions

### `RTXDI_StreamSample`

    bool RTXDI_StreamSample(
        inout RTXDI_Reservoir reservoir,
        uint lightIndex,
        float2 uv,
        float random,
        float targetPdf,
        float invSourcePdf)

Adds one light sample to the reservoir. Returns `true` if the sample was selected for the reservoir, `false` if not.

This function implements Algorithm (3) from the ReSTIR paper, "Streaming RIS using weighted reservoir sampling".

### `RTXDI_CombineReservoirs`

    bool RTXDI_CombineReservoirs(
        inout RTXDI_Reservoir reservoir,
        const RTXDI_Reservoir newReservoir,
        float random,
        float targetPdf)

Adds a reservoir with one sample into this reservoir. Returns `true` if the new reservoir's sample was selected, `false` if not. The new reservoir's `targetPdf` field is ignored and replaced with the `targetPdf` parameter of the function.

This function implements Algorithm (4) from the ReSTIR paper, "Combining the streams of multiple reservoirs".

### `RTXDI_FinalizeResampling`

    void RTXDI_FinalizeResampling(
        inout RTXDI_Reservoir reservoir,
        float normalizationNumerator,
        float normalizationDenominator)

Performs normalization of the reservoir after streaming. After this function is called, the reservoir's `weightSum` field becomes its inverse PDF that can be used for shading or for further reservoir combinations.

The `normalizationNumerator` and `normalizationDenominator` parameters specify the normalization scale for bias correction. Basic applications like streaming of initial light samples will set the numerator to 1.0 and the denominator to M (the number of samples in the reservoir). Spatio-temporal resampling will normally compute the numerator and denominator by weighing the final selected sample against the original surfaces used in resampling.

This function implements Equation (6) from the ReSTIR paper.


## Low-Level Sampling Functions

### `RTXDI_SamplePdfMipmap`

    void RTXDI_SamplePdfMipmap(
        inout RAB_RandomSamplerState rng, 
        Texture2D<float> pdfTexture,
        uint2 pdfTextureSize,
        out uint2 position,
        out float pdf)

Performs importance sampling of a set of items with their PDF values stored in a 2D texture mipmap. The texture must have power-of-2 dimensions and a mip chain up to 2x2 pixels (or 2x1 or 1x2 if the texture is rectangular). The mip chain must be generated using a regular 2x2 box filter, which means any standard way of generating a mipmap should work.

The function returns the position of the final selected texel in the `position` parameter, and its normalized selection PDF in the `pdf` parameter. If the PDF texture is empty or malformed (i.e. has four adjacent zeros in one mip level and a nonzero corresponding texel in the next mip level), the reutrned PDF will be zero.


### `RTXDI_PresampleLocalLights`

    void RTXDI_PresampleLocalLights(
        inout RAB_RandomSamplerState rng, 
        Texture2D<float> pdfTexture,
        uint2 pdfTextureSize,
        uint tileIndex,
        uint sampleInTile,
        RTXDI_ResamplingRuntimeParameters params,
        RWBuffer<uint2> RisBuffer)

Selects one local light using the provided PDF texture and stores its information in the RIS buffer at the position identified by the `tileIndex` and `sampleInTile` parameters. Additionally, stores compact light information in the companion buffer that is managed by the application, through the `RAB_StoreCompactLightInfo` function.

### `RTXDI_PresampleEnvironmentMap`

    void RTXDI_PresampleEnvironmentMap(
        inout RAB_RandomSamplerState rng, 
        Texture2D<float> pdfTexture,
        uint2 pdfTextureSize,
        uint tileIndex,
        uint sampleInTile,
        RTXDI_ResamplingRuntimeParameters params,
        RWBuffer<uint2> RisBuffer)

Selects one environment map texel using the provided PDF texture and stores its information in the RIS buffer at the position identified by the `tileIndex` and `sampleInTile` parameters.

### `RTXDI_PresampleLocalLightsForReGIR`

    void RTXDI_PresampleLocalLightsForReGIR(
        inout RAB_RandomSamplerState rng, 
        inout RAB_RandomSamplerState coherentRng,
        uint lightSlot,
        uint numSamples,
        RTXDI_ResamplingRuntimeParameters params, 
        RWBuffer<uint2> RisBuffer)

Selects one local light using RIS with `numSamples` proposals weighted relative to a specific ReGIR world space cell. The cell position and size are derived from its index; the cell index is derived from the `lightSlot` parameter: each cell contains a number of light slots packed together and stored in the RIS buffer. Additionally, stores compact light information in the companion buffer that is managed by the application, through the `RAB_StoreCompactLightInfo` function.

The weights of lights relative to ReGIR cells are computed using the [`RAB_GetLightTargetPdfForVolume`](RtxdiApplicationBridge.md#rab_getlighttargetpdfforvolume) application-defined function.

### `RTXDI_SampleLocalLights`

    RTXDI_Reservoir RTXDI_SampleLocalLights(
        inout RAB_RandomSamplerState rng, 
        inout RAB_RandomSamplerState coherentRng,
        RAB_Surface surface, 
        uint numSamples,
        RTXDI_ResamplingRuntimeParameters params,
        RWBuffer<uint2> RisBuffer,
        out RAB_LightSample o_selectedSample)

Selects one local light sample using RIS with `numSamples` proposals weighted relative to the provided `surface`, and returns a reservoir with the selected light sample. The sample itself is returned in the `o_selectedSample` parameter.

The proposals are picked from a RIS buffer tile that's picked using `coherentRng`, which should generate the same random numbers for a group of adjacent shader threads for performance. If the RIS buffer is not available, this function will fall back to uniform sampling from the local light pool, which is typically much more noisy. The RIS buffer must be pre-filled with samples using the [`RTXDI_PresampleLocalLights`](#rtxdi_presamplelocallights) function in a preceding pass.

### `RTXDI_SampleLocalLightsFromReGIR`

    RTXDI_Reservoir RTXDI_SampleLocalLightsFromReGIR(
        inout RAB_RandomSamplerState rng,
        inout RAB_RandomSamplerState coherentRng,
        RAB_Surface surface,
        uint numRegirSamples,
        uint numLocalLightSamples,
        RTXDI_ResamplingRuntimeParameters params,
        RWBuffer<uint2> RisBuffer,
        out RAB_LightSample o_selectedSample)

A variant of [`RTXDI_SampleLocalLights`](#rtxdi_samplelocallights) that samples from a ReGIR cell instead of the RIS buffer, if ReGIR is available and a cell exists at the surface position. If ReGIR is not available or the surface is out of bounds of the ReGIR spatial structure, the initial proposals will be drawn from the RIS buffer if that is available, or from the local lights with a uniform PDF.

The ReGIR cells are matched to the surface with jitter applied, and the magnitude of this jitter is specified in `params.regirCommon.samplingJitter`. The specific jitter offset is determined using the `coherentRng` generator, which should return the same random numbers for a group of adjacent shader threads for performance.

### `RTXDI_SampleInfiniteLights`

    RTXDI_Reservoir RTXDI_SampleInfiniteLights(
        inout RAB_RandomSamplerState rng, 
        RAB_Surface surface, 
        uint numSamples,
        RTXDI_ResamplingRuntimeParameters params,
        RWBuffer<uint2> RisBuffer,
        out RAB_LightSample o_selectedSample)

Selects one infinite light sample using RIS with `numSamples` proposals weighted relative to the provided `surface`, and returns a reservoir with the selected light sample. The sample itself is returned in the `o_selectedSample` parameter.

### `RTXDI_SampleEnvironmentMap`

    RTXDI_Reservoir RTXDI_SampleEnvironmentMap(
        inout RAB_RandomSamplerState rng, 
        inout RAB_RandomSamplerState coherentRng,
        RAB_Surface surface, 
        uint numSamples,
        RTXDI_ResamplingRuntimeParameters params,
        RWBuffer<uint2> RisBuffer,
        out RAB_LightSample o_selectedSample)

Selects one sample from the importance sampled environment light using RIS with `numSamples` proposals weighted relative to the provided `surface`, and returns a reservoir with the selected light sample. The sample itself is returned in the `o_selectedSample` parameter.

The proposals are picked from a RIS buffer tile, similar to [`RTXDI_SampleLocalLights`](#rtxdi_samplelocallights). The RIS buffer must be pre-filled with samples using the [`RTXDI_PresampleEnvironmentMap`](#rtxdi_presampleenvironmentmap) function in a preceding pass.


## High-Level Sampling and Resampling Functions

### `RTXDI_SampleLightsForSurface`

    RTXDI_Reservoir RTXDI_SampleLightsForSurface(
        inout RAB_RandomSamplerState rng,
        inout RAB_RandomSamplerState coherentRng,
        RAB_Surface surface,
        uint numRegirSamples,
        uint numLocalLightSamples,
        uint numInfiniteLightSamples,
        uint numEnvironmentMapSamples,
        RTXDI_ResamplingRuntimeParameters params, 
        RWBuffer<uint2> RisBuffer,
        out RAB_LightSample o_lightSample)

This function is a combination of `RTXDI_SampleLocalLightsFromReGIR` (or `RTXDI_SampleLocalLights` if compiled without ReGIR support), `RTXDI_SampleInfiniteLights`, and `RTXDI_SampleEnvironmentMap`. Reservoirs returned from each function are combined into one final reservoir, which is returned. 

### `RTXDI_TemporalResampling`

    struct RTXDI_TemporalResamplingParameters
    {
        float3 screenSpaceMotion;
        uint sourceBufferIndex;
        uint maxHistoryLength;
        uint biasCorrectionMode;
        float depthThreshold;
        float normalThreshold;
    };
    RTXDI_Reservoir RTXDI_TemporalResampling(
        uint2 pixelPosition,
        RAB_Surface surface,
        RTXDI_Reservoir curSample,
        RAB_RandomSamplerState rng,
        RTXDI_TemporalResamplingParameters tparams,
        RTXDI_ResamplingRuntimeParameters params,
        RWStructuredBuffer<RTXDI_PackedReservoir> LightReservoirs)

Implements the core functionality of the temporal resampling pass. Takes the previous G-buffer, motion vectors, and two light reservoir buffers - current and previous - as inputs. Tries to match the surfaces in the current frame to surfaces in the previous frame. If a match is found for a given pixel, the current and previous reservoirs are combined.

An optional visibility ray may be cast if enabled with the `tparams.biasCorrectionMode` setting, to reduce the resampling bias. That visibility ray should ideally be traced through the previous frame BVH, but can also use the current frame BVH if the previous is not available - that will produce more bias.

For more information on the members of the `RTXDI_TemporalResamplingParameters` structure, see the comments in the source code.

### `RTXDI_SpatialResampling`

    struct RTXDI_SpatialResamplingParameters
    {
        uint sourceBufferIndex;
        uint numSamples;
        uint numDisocclusionBoostSamples;
        uint targetHistoryLength;
        uint biasCorrectionMode;
        float samplingRadius;
        float depthThreshold;
        float normalThreshold;
    };
    RTXDI_Reservoir RTXDI_SpatialResampling(
        uint2 pixelPosition,
        RAB_Surface centerSurface,
        RTXDI_Reservoir centerSample,
        RAB_RandomSamplerState rng,
        RTXDI_SpatialResamplingParameters sparams,
        RTXDI_ResamplingRuntimeParameters params,
        RWStructuredBuffer<RTXDI_PackedReservoir> LightReservoirs,
        Buffer<float2> NeighborOffsets)


Implements the core functionality of the spatial resampling pass. Operates on the current frame G-buffer and its reservoirs. For each pixel, considers a number of its neighbors and, if their surfaces are similar enough to the current pixel, combines their light reservoirs.

Optionally, one visibility ray is traced for each neighbor being considered, to reduce bias, if enabled with the `sparams.biasCorrectionMode` setting.

For more information on the members of the `RTXDI_SpatialResamplingParameters` structure, see the comments in the source code.


### `RTXDI_SpatioTemporalResampling`

    struct RTXDI_SpatioTemporalResamplingParameters
    {
        float3 screenSpaceMotion;
        uint sourceBufferIndex;
        uint maxHistoryLength;
        uint biasCorrectionMode;
        float depthThreshold;
        float normalThreshold;
        uint numSamples;
        float samplingRadius;
    };
    RTXDI_Reservoir RTXDI_SpatioTemporalResampling(
        uint2 pixelPosition,
        RAB_Surface surface,
        RTXDI_Reservoir curSample,
        RAB_RandomSamplerState rng,
        RTXDI_SpatioTemporalResamplingParameters stparams,
        RTXDI_ResamplingRuntimeParameters params,
        RWStructuredBuffer<RTXDI_PackedReservoir> LightReservoirs,
        Buffer<float2> NeighborOffsets)

Implements the core functionality of a combined spatio-temporal resampling pass. This is similar to a sequence of `RTXDI_TemporalResampling` and `RTXDI_SpatialResampling`, with the exception that the input reservoirs are all taken from the previous frame. This function is useful for implementing a lighting solution in a single shader, which generates the initial samples, applies spatio-temporal resampling, and shades the final samples.

### `RTXDI_BoilingFilter`

    void RTXDI_BoilingFilter(
        uint2 LocalIndex,
        float filterStrength,
        RTXDI_ResamplingRuntimeParameters params,
        inout RTXDI_Reservoir state)

Applies a boiling filter over all threads in the compute shader thread group. This filter attempts to reduce boiling by removing reservoirs whose weight is significantly higher than the weights of their neighbors. Essentially, when some lights are important for a surface but they are also unlikely to be located in the initial sampling pass, ReSTIR will try to hold on to these lights by spreading them around, and if such important lights are sufficiently rare, the result will look like light bubbles appearing and growing, then fading. This filter attempts to detect and remove such rare lights, trading boiling for bias.

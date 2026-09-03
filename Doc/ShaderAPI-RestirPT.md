# RTXDI Shader API for ReSTIR PT

This document lists the macros, structures and functions that are specific to the ReSTIR PT subsystem of RTXDI, which resamples full indirect-lighting *paths* (rather than single light samples). Signatures match the headers under [`Libraries/Rtxdi/Include/Rtxdi/PT/`](../Libraries/Rtxdi/Include/Rtxdi/PT/).

ReSTIR PT reuses paths between neighbors and across frames using a hybrid shift (random replay plus reconnection). This page is the reference for the runtime symbols; for the algorithm and the required application path-tracer bridge, see [RestirPT.md](RestirPT.md) and [RtxdiApplicationBridge.md](RtxdiApplicationBridge.md). As with the rest of RTXDI, include the appropriate `PT/` header(s) after defining (or declaring) the bridge functions and resource macros below.


## User-Defined Macros

### `RTXDI_PT_RESERVOIR_BUFFER`

Define this macro to a resource name for the PT reservoir buffer, which should have HLSL type `RWStructuredBuffer<RTXDI_PackedPTReservoir>`. Required by [`PT/Reservoir.hlsli`](../Libraries/Rtxdi/Include/Rtxdi/PT/Reservoir.hlsli).

### `RTXDI_NEIGHBOR_OFFSETS_BUFFER`

Define this macro to a resource name for the neighbor offset buffer, which should have HLSL type `Buffer<float2>`. Used by spatial resampling to pick neighbor pixels.

### `RTXDI_SPATIAL_NEIGHBOR_SELECTION_BUFFER`

Define this macro to a resource name (HLSL type `RWByteAddressBuffer`) that stores the pre-selected spatial neighbors produced by [`RTXDI_PTSpatialNeighborSelection`](#rtxdi_ptspatialneighborselection) and consumed by [`RTXDI_PTSpatialResampling`](#rtxdi_ptspatialresampling) when compatibility-guided neighbor selection is enabled.

The buffer holds `SPATIAL_HEURISTIC_MAX_NEIGHBORS` 32-bit entries per pixel. Entry `k` for the pixel with linear index `p = pixel.y * viewportSize.x + pixel.x` is stored at byte offset `(p * SPATIAL_HEURISTIC_MAX_NEIGHBORS + k) * 4`. Each entry packs a neighbor pixel coordinate as `x | (y << 16)` (16 bits each), or the sentinel `SPATIAL_HEURISTIC_INVALID_NEIGHBOR` (`0xFFFFFFFF`) when the slot holds no neighbor.

### Duplication-map resources

The duplication-based history reduction and the stagnancy-driven final-shading decorrelation use a small set of screen-space textures. Define these macros to the corresponding application resources:

- `RTXDI_PT_SAMPLE_ID_TEXTURE` — `RWTexture2D<uint>`, per-pixel sample ID.
- `RTXDI_PT_DUPLICATION_MAP` — `RWTexture2D<float2>`, `.x` = spatial duplication count, `.y` = temporal stagnancy.
- `RTXDI_PT_SMOOTHED_DUPLICATION_MAP` — `RWTexture2D<float>`, current-frame smoothed stagnancy output.
- `RTXDI_PT_PREV_SMOOTHED_DUPLICATION_MAP` — `Texture2D<float>`, previous-frame smoothed stagnancy (for reprojection).

### `RTXDI_ENABLE_BOILING_FILTER` and `RTXDI_BOILING_FILTER_GROUP_SIZE`

Define `RTXDI_ENABLE_BOILING_FILTER` to enable [`RTXDI_PTBoilingFilter`](#rtxdi_ptboilingfilter). It relies on group-shared memory and wave intrinsics, so it is only available in the compute-shader variant of a pass. `RTXDI_BOILING_FILTER_GROUP_SIZE` must be set to the thread-group edge length (e.g. `RTXDI_SCREEN_SPACE_GROUP_SIZE`) before the boiling filter is included.

### `RTXDI_PT_ENABLE_DUPLICATION_MAP_COUNT`

Define this macro before including [`PT/DuplicationMap.hlsli`](../Libraries/Rtxdi/Include/Rtxdi/PT/DuplicationMap.hlsli) to compile the group-shared duplication-count reduction [`RTXDI_PTComputeDuplicationMap`](#rtxdi_ptcomputeduplicationmap). It is kept behind a macro so the `groupshared` allocation is not pulled into the ray-generation resampling shaders that include the header only for the other duplication-map functions.


## Structures

### `RTXDI_RuntimeParameters` and `RTXDI_ReservoirBufferParameters`

Shared with ReSTIR DI/GI; see [`RtxdiParameters.h`](../Libraries/Rtxdi/Include/Rtxdi/RtxdiParameters.h) and the [Shader API](ShaderAPI.md).

### `RTXDI_PackedPTReservoir`

A compact representation of a single path reservoir that should be stored in a structured buffer.

### `RTXDI_PTReservoir`

Represents a single resampled path: the reconnection-vertex position and normal (`translatedWorldPosition`, `worldNormal`), incoming `radiance`, RIS running sum of resampling weights / Unbiased Contribution Weight (`weightSum`), effective sample count (confidence weight) (`M`), the path contribution (`targetFunction`), path metadata (`age`, `rcVertexLength`, `pathLength`), reconnection data (`partialJacobian`, `rcWiPdf`), and the RNG seed/index (`randomSeed`, `randomIndex`) used to replay the path. `age` doubles as the duplication-map stagnancy signal when age-based rejection is disabled. A single reused scratch bit (`auxFlag`) is accessed only through the flag accessors below. Serialize with `RTXDI_PackPTReservoir` and deserialize with `RTXDI_UnpackPTReservoir`. Prefer [`PT/Reservoir.hlsli`](../Libraries/Rtxdi/Include/Rtxdi/PT/Reservoir.hlsli) for the authoritative member list.

### `RTXDI_PTBufferIndices`

Selects the reservoir buffer slots used by each pass (initial output, temporal in/out, spatial in/out, final-shading input, and the preserved initial-sample slot). Two slots ping-pong for resampling I/O; a third preserves the unresampled initial-sampling reservoir for the final-shading decorrelation fallback. Defined in [`PT/ReSTIRPTParameters.h`](../Libraries/Rtxdi/Include/Rtxdi/PT/ReSTIRPTParameters.h).

### `RTXDI_PTInitialSamplingParameters`

Path tracer sampling controls (`numInitialSamples`, `maxBounceDepth`, `maxRcVertexLength`). Defined in [`PT/ReSTIRPTParameters.h`](../Libraries/Rtxdi/Include/Rtxdi/PT/ReSTIRPTParameters.h).

### `RTXDI_PTDecorrelationParameters`

Controls the final-shading decorrelation fallback (swap the resampled reservoir for the preserved, unresampled initial-sampling reservoir) and its firefly replacement: `decorrelationMode`, `decorrelationFactor`, `decorrelationStagnancyExponent`, `decorrelationEmaFactor`, `fireflyReplacementFilterEnable`, `fireflyReplacementFilterStrength`, `fireflyReplacementBiasReduction`, and `fireflyReplacementMultiplyBound`. Stored as `RTXDI_PTParameters::decorrelation`. Defined in [`PT/ReSTIRPTParameters.h`](../Libraries/Rtxdi/Include/Rtxdi/PT/ReSTIRPTParameters.h).

### Resampling, reconnection, and hybrid-shift parameter structs

`RTXDI_PTTemporalResamplingParameters`, `RTXDI_PTSpatialResamplingParameters`, `RTXDI_PTReconnectionParameters`, and `RTXDI_PTHybridShiftPerFrameParameters` are defined in [`PT/ReSTIRPTParameters.h`](../Libraries/Rtxdi/Include/Rtxdi/PT/ReSTIRPTParameters.h). Prefer that header for authoritative member lists.

### Runtime parameter structs

`RTXDI_PTInitialSamplingRuntimeParameters`, `RTXDI_PTTemporalResamplingRuntimeParameters`, and `RTXDI_PTSpatialResamplingRuntimeParameters` carry the per-invocation inputs an application fills before calling the corresponding entry point (pixel/reservoir position, camera positions, motion vector, viewport size). Each is defined next to its pass in the `PT/` headers, with an `RTXDI_EmptyPT*RuntimeParameters()` helper that returns a zero-initialized instance.

### `RTXDI_PTDecorrelationMode`

`None`, `Uniform`, or `Stagnancy` (mirrors the `RTXDI_PT_DECORRELATION_MODE_*` shader constants). Selects how `RTXDI_PTDecorrelationParameters::decorrelationFactor` is interpreted by final shading.

### `RTXDI_PTReconnectionMode`

`FixedThreshold` or `Footprint` (mirrors the `RTXDI_RESTIRPT_RECONNECTION_MODE_*` constants). Selects how the hybrid shift decides whether a vertex pair is reconnectible.


## Application Bridge Requirements

Beyond the reservoir/resource macros above, ReSTIR PT requires the application to provide a path tracer and a small number of `RAB_` bridge functions. Most of these are shared with the path-tracing integration described in [RestirPT.md](RestirPT.md) (notably `RAB_PathTrace`, the `RAB_PathTracerUserData` struct with `RAB_PathTracerUserDataSetPathType`, and the MIS callback `RAB_GetMISWeightForNEE`). The neighbor-selection runtime function additionally requires:

- `RAB_GetNeighborSelectionSurface(int2 pixel, out float3 worldNormal, out float3 worldPos, out float viewDepth)` — reads the neighbor-selection G-buffer and reconstructs a world position, returning `false` for background pixels. Used by [`RTXDI_PTSpatialNeighborSelection`](#rtxdi_ptspatialneighborselection). The Full Sample implements this in [`RAB_NeighborSelection.hlsli`](../Samples/FullSample/Shaders/LightingPasses/RtxdiApplicationBridge/RAB_NeighborSelection.hlsli).


## Reservoir Functions

### `RTXDI_EmptyPTReservoir`

    RTXDI_PTReservoir RTXDI_EmptyPTReservoir()

Returns an empty reservoir object.

### `RTXDI_IsValidPTReservoir`

    bool RTXDI_IsValidPTReservoir(const RTXDI_PTReservoir reservoir)

Returns `true` if the reservoir contains a valid path sample (`M > 0`).

### `RTXDI_MakePTReservoir`

    RTXDI_PTReservoir RTXDI_MakePTReservoir(
        const float3 TargetFunction,
        const uint RandomSeed,
        const uint RandomIndex,
        const uint RcVertexLength,
        const uint PathLength,
        const float PartialJacobian,
        const float RcWiPdf,
        const float3 TranslatedWorldPosition,
        const float3 WorldNormal,
        const float3 Radiance,
        const float SamplePdf)

Creates a path reservoir from a raw path sample. The original sample PDF may be embedded into `Radiance`, in which case `SamplePdf` should be `1.0`.

### `RTXDI_GetRngForShading`

    RTXDI_RandomSamplerState RTXDI_GetRngForShading(RTXDI_PTReservoir reservoir)

Returns an RNG initialized from the reservoir's stored seed and advanced to the state required to replay the reservoir's path during final shading (see [RestirPT.md](RestirPT.md)).

### `RTXDI_LoadPTReservoir`

    RTXDI_PTReservoir RTXDI_LoadPTReservoir(
        RTXDI_ReservoirBufferParameters reservoirParams,
        uint2 reservoirPosition,
        uint reservoirArrayIndex)

Loads and unpacks a reservoir from `RTXDI_PT_RESERVOIR_BUFFER`. The buffer contains multiple 2D arrays of reservoirs (corresponding to screen pixels), so the function translates the reservoir position and array index to the buffer index.

### `RTXDI_StorePTReservoir`

    void RTXDI_StorePTReservoir(
        const RTXDI_PTReservoir reservoir,
        RTXDI_ReservoirBufferParameters reservoirParams,
        uint2 reservoirPosition,
        uint reservoirArrayIndex)

Packs and stores a reservoir into `RTXDI_PT_RESERVOIR_BUFFER`. Buffer addressing works like `RTXDI_LoadPTReservoir`. `RTXDI_PackPTReservoir` / `RTXDI_UnpackPTReservoir` are available separately for callers that manage storage themselves.

### Reservoir flag accessors

    bool RTXDI_GetShouldBoostSpatialSamples(const RTXDI_PTReservoir reservoir)
    void RTXDI_SetShouldBoostSpatialSamples(inout RTXDI_PTReservoir reservoir, bool value)
    bool RTXDI_GetFireflyDetected(const RTXDI_PTReservoir reservoir)
    void RTXDI_SetFireflyDetected(inout RTXDI_PTReservoir reservoir, bool value)

Semantic accessors over a single reused scratch bit (`auxFlag`) in the reservoir. `ShouldBoostSpatialSamples` is written by temporal resampling and read by spatial resampling (duplication-based MCap boost); `FireflyDetected` is written at the end of spatial resampling and read by final shading (firefly replacement). Only one meaning is live at a time.


## Basic Resampling Functions

### `RTXDI_CombinePTReservoirs`

    bool RTXDI_CombinePTReservoirs(
        inout RTXDI_PTReservoir targetReservoir,
        RTXDI_PTReservoir NewReservoir,
        float Random,
        float3 NewTargetFunction)

Adds a reservoir with one sample as a candidate sample into `targetReservoir`. Returns `true` if the new reservoir's sample was selected. 

### `RTXDI_InternalSimplePTResample`

    bool RTXDI_InternalSimplePTResample(
        inout RTXDI_PTReservoir targetReservoir,
        const RTXDI_PTReservoir NewReservoir,
        float Random,
        float3 NewTargetFunction,
        float SampleNormalization,
        float SampleM)

General form of reservoir combination that lets the caller specify the sample normalization and `M` directly rather than deriving them from `NewReservoir`. Returns `true` if the new sample was selected.

### `RTXDI_FinalizePTResampling`

    void RTXDI_FinalizePTResampling(
        inout RTXDI_PTReservoir reservoir,
        float Numerator,
        float Denominator)

Normalizes the reservoir after streaming; multiplies `reservoir.weightSum` by numerator / denominator; afterwards `weightSum` becomes the unbiased contribution weight (inverse PDF estimate) used for shading or further combination. 


## High-Level Sampling and Resampling Functions

### `GenerateInitialSamples`

    RTXDI_PTReservoir GenerateInitialSamples(
        RTXDI_PTInitialSamplingParameters initialSamplingParams,
        RTXDI_PTInitialSamplingRuntimeParameters isrParams,
        RTXDI_PTReconnectionParameters rcParams,
        RTXDI_PathTracerRandomContext ptRandContext,
        RAB_Surface surface,
        inout RAB_PathTracerUserData ptud)

Traces `initialSamplingParams.numInitialSamples` path trees from `surface` (via the application `RAB_PathTrace`). During its generation, each path tree resamples a path (estimates the indirect lighting contribution of the whoe path space) by RIS. These paths are again streamed into a reservoir with RIS, and returns the finalized initial-sampling reservoir. The random seed and/or reconnection data recorded during tracing are stored on the reservoir so later passes can shift the path.

### `RTXDI_PTTemporalResampling`

    RTXDI_PTReservoir RTXDI_PTTemporalResampling(
        RTXDI_PTTemporalResamplingParameters tParams,
        RTXDI_PTTemporalResamplingRuntimeParameters trrParams,
        RTXDI_PTHybridShiftPerFrameParameters hspfParams,
        RTXDI_PTReconnectionParameters rcParams,
        RTXDI_RuntimeParameters rParams,
        RTXDI_ReservoirBufferParameters bufferParams,
        RTXDI_RandomSamplerState rng,
        RTXDI_PTBufferIndices bufferIndices,
        inout bool selectedPrevSample,
        inout RAB_PathTracerUserData ptud)

Implements the core of the temporal resampling pass. Reprojects the current pixel into the previous frame using `trrParams.motionVector`, and if a matching surface is found, shifts the previous path into the current domain (hybrid shift) and combines it with the current reservoir. `selectedPrevSample` reports whether a previous-frame sample was chosen. Per-pass tunables are in `tParams`; per-invocation inputs (pixel/reservoir position, camera positions, motion vector) are in `trrParams`.

### `RTXDI_PTSpatialResampling`

    RTXDI_PTReservoir RTXDI_PTSpatialResampling(
        RTXDI_PTSpatialResamplingRuntimeParameters srrParams,
        RTXDI_PTSpatialResamplingParameters spatialParams,
        RTXDI_PTHybridShiftPerFrameParameters hspfParams,
        RTXDI_PTReconnectionParameters rcParams,
        RTXDI_ReservoirBufferParameters reservoirBufferParams,
        RTXDI_PTBufferIndices bufferIndices,
        RTXDI_RuntimeParameters rParams,
        RTXDI_RandomSamplerState rng,
        inout bool resampled,
        inout RAB_PathTracerUserData ptud)

Implements the core of the spatial resampling pass. For each pixel, considers a number of neighbors and, if their surfaces are similar enough, shifts their paths into the current domain (hybrid shift) and combines their reservoirs. When `spatialParams.enableSpatialHeuristicMode` is set (compatibility-guided neighbor selection), neighbors are read from `RTXDI_SPATIAL_NEIGHBOR_SELECTION_BUFFER` (populated by [`RTXDI_PTSpatialNeighborSelection`](#rtxdi_ptspatialneighborselection)); otherwise they are drawn uniformly from `RTXDI_NEIGHBOR_OFFSETS_BUFFER`. `resampled` reports whether any neighbor sample was selected. Per-pass tunables are in `spatialParams`; per-invocation inputs are in `srrParams`.

### `RTXDI_PTBoilingFilter`

Compiled only when `RTXDI_ENABLE_BOILING_FILTER` is defined.

    void RTXDI_PTBoilingFilter(
        uint2 LocalIndex,
        float filterStrength,
        inout RTXDI_PTReservoir reservoir)

Applies a boiling filter over all threads in the compute-shader thread group, emptying reservoirs whose weighted contribution is significantly higher than their neighbors'. This is the same idea as `RTXDI_BoilingFilter` for direct lighting; see the [Shader API](ShaderAPI.md#rtxdi_boilingfilter) for the rationale.


## Neighbor Selection and Duplication Map

These helpers support two optional ReSTIR PT features: compatibility-guided neighbor selection (pre-selecting compatible neighbors for spatial resampling) and a duplication map (driving duplication-based history reduction and the stagnancy-based final-shading decorrelation). Each is the body of a dedicated screen-space pass; the sample wraps them in thin entry-point shaders. See [RestirPT.md](RestirPT.md) for the algorithms and paper references.

### `RTXDI_PTSpatialNeighborSelection`

    void RTXDI_PTSpatialNeighborSelection(
        uint2 pixelPosition,
        uint2 viewportSize,
        uint frameIndex,
        uint numSpatialSamples)

Compatibility-guided neighbor selection ([Junkins et al.](https://doi.org/10.1145/3820024)). For each pixel, runs weighted reservoir sampling over a disk of candidate pixels, scoring each by a continuous geometric compatibility heuristic (normal + primary-hit-position similarity from `RAB_GetNeighborSelectionSurface`), and writes up to `min(numSpatialSamples, SPATIAL_HEURISTIC_MAX_NEIGHBORS)` selected neighbors into `RTXDI_SPATIAL_NEIGHBOR_SELECTION_BUFFER` (packed as `x | (y << 16)`; see that macro for the exact layout). `RTXDI_PTSpatialResampling` consumes the result when `enableSpatialHeuristicMode` is set. See [RestirPT.md](RestirPT.md#compatibility-guided-spatial-neighbor-selection) for the algorithm.

### `RTXDI_PTNeedsDuplicationMap`

    bool RTXDI_PTNeedsDuplicationMap(
        RTXDI_PTTemporalResamplingParameters temporalResampling,
        RTXDI_PTDecorrelationParameters decorrelation)

Returns true when any feature that consumes the duplication map is enabled: duplication-based history reduction, or stagnancy-based final-shading decorrelation. Defined in [`PT/Decorrelation.hlsli`](../Libraries/Rtxdi/Include/Rtxdi/PT/Decorrelation.hlsli). Host code can use `rtxdi::NeedsDuplicationMap` from [`PT/ReSTIRPT.h`](../Libraries/Rtxdi/Include/Rtxdi/PT/ReSTIRPT.h).

### `RTXDI_PTNeedsDuplicationInputs`

    bool RTXDI_PTNeedsDuplicationInputs(
        uint outputBufferIndex,
        RTXDI_PTParameters restirPT)

Returns true when this resampling pass should write per-pixel duplication-map inputs: the pass produces the reservoir that final shading will read (`outputBufferIndex == restirPT.bufferIndices.finalShadingInputBufferIndex`), and [`RTXDI_PTNeedsDuplicationMap`](#rtxdi_ptneedsduplicationmap) is true. Call this from temporal and spatial resampling before [`RTXDI_PTStoreDuplicationInputs`](#rtxdi_ptstoreduplicationinputs). Defined in [`PT/Decorrelation.hlsli`](../Libraries/Rtxdi/Include/Rtxdi/PT/Decorrelation.hlsli).

### `RTXDI_PTStoreDuplicationInputs`

    void RTXDI_PTStoreDuplicationInputs(uint2 pixelPosition, RTXDI_PTReservoir reservoir)

Stores the per-pixel inputs the duplication map is built from: the reservoir's sample ID (0 for empty reservoirs) into `RTXDI_PT_SAMPLE_ID_TEXTURE`, and its normalized stagnancy (`age / 40`) into the `.y` channel of `RTXDI_PT_DUPLICATION_MAP`. Call this from the last resampling pass when [`RTXDI_PTNeedsDuplicationInputs`](#rtxdi_ptneedsduplicationinputs) is true.

### `RTXDI_PTComputeDuplicationMap`

Compiled only when `RTXDI_PT_ENABLE_DUPLICATION_MAP_COUNT` is defined (uses group-shared memory).

    void RTXDI_PTComputeDuplicationMap(uint2 groupID, uint2 threadID, uint2 viewportSize)

For each pixel, counts how many neighbors in a 17×17 window share the same (non-zero) sample ID and writes the normalized count into the `.x` channel of `RTXDI_PT_DUPLICATION_MAP`. Dispatched as 16×16 thread groups; each group cooperatively loads a tile of `RTXDI_PT_SAMPLE_ID_TEXTURE` into LDS. Feeds duplication-based temporal history reduction (`RAB_GetDuplicationMapCount`).

### `RTXDI_PTComputeSmoothedDuplicationMap`

    void RTXDI_PTComputeSmoothedDuplicationMap(
        uint2 pixelPosition,
        uint2 viewportSize,
        float emaFactor,
        int2 prevPixel,
        bool prevValid)

Temporally smooths the duplication map's stagnancy channel: blends the current-frame value (`RTXDI_PT_DUPLICATION_MAP.y`) with a spatially-averaged previous-frame value (`RTXDI_PT_PREV_SMOOTHED_DUPLICATION_MAP`) using an exponential moving average, and writes the result to `RTXDI_PT_SMOOTHED_DUPLICATION_MAP`. The caller supplies the previous-frame reprojection (`prevPixel`, and `prevValid` = whether it is on-screen), since motion-vector reprojection is application-specific. The smoothed signal drives the Stagnancy-mode final-shading decorrelation probability (`emaFactor` = `RTXDI_PTDecorrelationParameters::decorrelationEmaFactor`).


## Decorrelation

Final-shading helpers that swap the resampled reservoir for the preserved initial-sampling reservoir. Defined in [`PT/Decorrelation.hlsli`](../Libraries/Rtxdi/Include/Rtxdi/PT/Decorrelation.hlsli). See [RestirPT.md](RestirPT.md#decorrelation-for-dlss-ray-reconstruction) for the algorithm.

### `RTXDI_PTNeedsPreservedInitialSample`

    bool RTXDI_PTNeedsPreservedInitialSample(RTXDI_PTDecorrelationParameters decorrelation)

Returns true when initial sampling should copy the unresampled reservoir into `initialPathTracerPreservedBufferIndex` so final shading can fall back to it.

### `RTXDI_PTDetectDecorrelationFireflies`

    void RTXDI_PTDetectDecorrelationFireflies(
        uint2 localIndex,
        inout RTXDI_PTReservoir reservoir,
        RTXDI_PTDecorrelationParameters decorrelation,
        RTXDI_PTBufferIndices bufferIndices)

Clears the firefly flag, then (when `RTXDI_ENABLE_BOILING_FILTER` is defined and firefly replacement is enabled) runs the group boiling filter and flags outlier pixels. Call from spatial resampling before storing the reservoir. The compute-shader variant must define `RTXDI_ENABLE_BOILING_FILTER` and `RTXDI_BOILING_FILTER_GROUP_SIZE` before including `Decorrelation.hlsli`.

### `RTXDI_PTApplyDecorrelation`

    float RTXDI_PTApplyDecorrelation(
        uint2 pixelPosition,
        uint2 reservoirPosition,
        inout RTXDI_PTReservoir ptReservoir,
        RTXDI_PTDecorrelationParameters decorrelation,
        RTXDI_PTBufferIndices bufferIndices,
        RTXDI_ReservoirBufferParameters reservoirBuffer,
        uint frameIndex)

Applies Uniform or Stagnancy-mode decorrelation (and firefly replacement) to `ptReservoir`, optionally replacing it with the preserved initial-sampling reservoir. Returns the per-pixel decorrelation probability actually used (0 when inactive). Reads `RTXDI_PT_SMOOTHED_DUPLICATION_MAP` and `RTXDI_PT_DUPLICATION_MAP`. Call from final shading after loading the resampled reservoir.


## Hybrid Shift

### `RTXDI_ComputeHybridShift`

    void RTXDI_ComputeHybridShift(
        inout RAB_Surface surfaceForResampling,
        inout RTXDI_PTReservoir neighborSample,
        RTXDI_PTHybridShiftPerFrameParameters hspfParams,
        RTXDI_PTHybridShiftRuntimeParameters hsrParams,
        RTXDI_PTReconnectionParameters rcParams,
        inout float3 targetFunction,
        inout float jacobian,
        inout RAB_PathTracerUserData ptud)

Shifts `neighborSample`'s path into `surfaceForResampling`'s domain, combining reconnection at the reconnection vertex with random replay of the prefix, and outputs the shifted path's `targetFunction` and the shift `jacobian`. This is the primitive that both temporal and spatial resampling use internally; applications normally don't call it directly. It re-enters the application `RAB_PathTrace` for random replay and relies on the MIS callbacks for NEE-sampled reconnection vertices. See [RestirPT.md](RestirPT.md) for the full algorithm and the required application path-tracer bridge.

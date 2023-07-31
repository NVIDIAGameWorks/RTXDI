
# RTXDI Application Bridge

The application must implement a number of structures and functions on the shader side that are necessary for the RTXDI resampling functions to operate. These structures and functions must be declared before including the main RTXDI header file, [`ResamplingFunctions.hlsli`](../rtxdi-sdk/include/rtxdi/ResamplingFunctions.hlsli).

A reference implementation of the bridge functions and structures with support for multiple light types and fractional visibility (translucency) can be found in [`RtxdiApplicationBridge.hlsli`](../shaders/LightingPasses/RtxdiApplicationBridge.hlsli). This implementation uses some functionality defined in other header files, most notably, polymorphic lights are implemented in [`PolymorphicLight.hlsli`](../shaders/PolymorphicLight.hlsli).

Below is a list of structures and functions that need to be implemented by the application, by category.

## Structures

All these structures are opaque, i.e. the library code makes no assumption about their members. Instances of these structures are only generated and consumed by other functions defined by the application.

### `RAB_Surface`

Stores information about a surface, including its position, orientation, and material parameters. In addition to that, a view direction should also be a part of the `RAB_Surface` structure. This structure must contain everything that is necessary to compute a material BRDF using the `RAB_GetLightSampleTargetPdfForSurface` function.

Instances of `RAB_Surface` are constructed in the `RAB_GetGBufferSurface` function, which loads the surface information from either the current or the previous G-buffer given a pixel position, and is called from the temporal and spatial resampling functions. Alternatively, these instances can be produced directly in the primary rays shader, for example, and passed into the resampling functions as the current surface being shaded.

### `RAB_LightInfo`

Stores information about a polymorphic light, i.e. a light of any type. Typically, this structure would contain a field encoding the light type, another field storing the light radiance, and other fields like position and orientation, whose interpretation depends on the specific light type. It's not a requirement however, and an implementation could choose to store lights of different types in different arrays, and keep only the light type and array index in the `RAB_LightInfo` structure, loading the specific light information only when sampling or weighing the light is performed.

`RAB_LightInfo` is initially returned by the `RAB_LoadLightInfo` function. In the pre-sampling passes, a light can be stored in the RIS buffer using the `RAB_StoreCompactLightInfo` function and later loaded from that buffer using the `RAB_LoadCompactLightInfo` function. This compact storage is optional and only improves performance when there are hundreds of thousands of lights or more.

Ultimately, `RAB_LightInfo` instances are consumed by the `RAB_SamplePolymorphicLight` and `RAB_GetLightTargetPdfForVolume` functions.

### `RAB_LightSample`

Represents a point on a light and its radiance, weighted relative to the surface that was used to generate the sample. Light samples are produced by the `RAB_SamplePolymorphicLight` function which takes a `RAB_LightInfo`, a `RAB_Surface`, and a pair of random numbers. Internally, the instances of `RAB_LightSample` are only used to compute the target PDF through `RAB_GetLightSampleTargetPdfForSurface` and are not stored anywhere. Light samples that are stored and reused by ReSTIR are stored as sample references, or instances of `RTXDI_LightSampleRef` structure that only stores the light index and the random numbers. Then the actual position on the lights are re-calculated for each surface they are weighed against.

The application can use the same `RAB_LightSample` instances to compute final shading, as shown in [`ShadingHelpers.hlsli`](../shaders/LightingPasses/ShadingHelpers.hlsli).

### `RAB_RandomSamplerState`

Stores the mutable state of a random number generator (RNG), that is, the random seed, sample index, and anything else that is necessary to generate random numbers. The application is expected to initialize the random sampler on its own and pass the state to the sampling functions. The sampling functions then pass the RNG state to [`RAB_GetNextRandom`](#rab_getnextrandom), multiple times per resampling function call.


## Constructors

### `RAB_EmptySurface`

`RAB_Surface RAB_EmptySurface()`

Returns an empty `RAB_Surface` object. It is expected that `RAB_IsSurfaceValid` returns `false` when such object is passed to it.

### `RAB_EmptyLightInfo`

`RAB_LightInfo RAB_EmptyLightInfo()`

Returns an empty `RAB_LightInfo` object.

### `RAB_EmptyLightSample`

`RAB_LightSample RAB_EmptyLightSample()`

Returns an empty `RAB_LightSample` object.


## G-buffer Input and Accessor Functions

### `RAB_GetGBufferSurface`

`RAB_Surface RAB_GetGBufferSurface(int2 pixelPosition, bool previousFrame)`

Loads a surface from the current or previous G-buffer at the specified pixel position. Pixel positions may be out-of-bounds or negative, in which case the function is supposed to return an invalid surface. Invalid surfaces are identified by `RAB_IsSurfaceValid` returning `false`.

### `RAB_IsSurfaceValid`

`bool RAB_IsSurfaceValid(RAB_Surface surface)`

Tests if the provided surface contains valid geometry. This function should return `false` for surfaces loaded from out-of-bounds pixels or from pixels containing the sky or other backgrounds.

### `RAB_GetSurfaceWorldPos`

`float3 RAB_GetSurfaceWorldPos(RAB_Surface surface)`

Returns the world position of the provided surface.

### `RAB_GetSurfaceNormal`

`float3 RAB_GetSurfaceNormal(RAB_Surface surface)`

Returns the world space shading normal of the provided surface. Normals are used only to determine surface similarity in temporal and spatial resampling functions.

### `RAB_GetSurfaceLinearDepth`

`float RAB_GetSurfaceLinearDepth(RAB_Surface surface)`

Returns the linear depth of the provided surface. It doesn't have to be linear depth in a strict sense (i.e. `viewPosition.z`), and can be distance to the camera or primary path length instead. The motion vectors provided to `RTXDI_TemporalResampling` or `RTXDI_SpatioTemporalResampling` must have their .z component computed as the difference between the linear depth of the same surface computed on the previous frame and the current frame.

### `RAB_ClampSamplePositionIntoView`

`int2 RAB_ClampSamplePositionIntoView(int2 pixelPosition, bool previousFrame)`

This function is called in the spatial resampling passes to make sure that the samples actually land on the screen and not outside of its boundaries. It can clamp the position or reflect it about the nearest screen edge. The simplest implementation will just return the input pixelPosition.

## Lights and Samples

### `RAB_LoadLightInfo`

`RAB_LightInfo RAB_LoadLightInfo(uint index, bool previousFrame)`

Loads the information about a polymorphic light based on its index, on the current or previous frame. See [`RAB_LightInfo`](#rab_lightinfo) for the description of what information is required. The indices passed to this function will be in one of the three ranges provided to `rtxdi::Context::FillRuntimeParameters(...)`:

- `firstLocalLight` ... `firstLocalLight + numLocalLights - 1`: local lights;
- `firstInfiniteLight` ... `firstInfiniteLight + numInfiniteLights - 1` : infinite lights;
- `environmentLightIndex`: importance sampled environment light, if `environmentLightPresent` is `true`.

These ranges do not have to be continuously packed in one buffer or start at zero. The application can choose to use some higher bits in the light index to store information. The lower 31 bit of the light index are available; the highest bit is reserved for internal use.

### `RAB_StoreCompactLightInfo`

`bool RAB_StoreCompactLightInfo(uint linearIndex, RAB_LightInfo lightInfo)`

Stores the information about a polymorphic light in a compacted form in the RIS buffer location at `linearIndex`. This buffer is populated during the local light presampling and ReGIR presampling passes. If the light can be compacted, this function does the store and returns `true`; if the light cannot be compacted, it doesn't store and returns `false`.

What exactly does "compacted" mean here is up to the implementation, but the intent is to allow storage of the most common light types in a densely packed buffer: for example, if a light without shaping information can be stored in 2/3 of the memory required to store a light with shaping information, and 99% of lights in the scene are not shaped, then it makes sense to compact the unshaped lights and load the shaped lights from the source buffer using [`RAB_LoadLightInfo`](#rab_loadlightInfo).

### `RAB_LoadCompactLightInfo`

`RAB_LightInfo RAB_LoadCompactLightInfo(uint linearIndex)`

Loads the information about a polymorphic light that is stored in a compacted form in the RIS buffer location at `linearIndex`. These compacted versions of lights are only stored through [`RAB_StoreCompactLightInfo`](#rab_storecompactlightinfo); the application is not expected to pre-populate the RIS buffer with lights.

### `RAB_TranslateLightIndex`

`int RAB_TranslateLightIndex(uint lightIndex, bool currentToPrevious)`

Translates the light index from the current frame to the previous frame (if `currentToPrevious` is `true`) or from the previous frame to the current frame (if `currentToPrevious` is `false`). Returns the new index, or a negative number if the light does not exist in the other frame.


## Sampling Functions

### `RAB_GetLightSampleTargetPdfForSurface`

`float RAB_GetLightSampleTargetPdfForSurface(RAB_LightSample lightSample, RAB_Surface surface)`

Computes the weight of the given light samples when the given surface is shaded using that light sample. Exact or approximate BRDF evaluation can be used to compute the weight. ReSTIR will converge to a correct lighting result even if all samples have a fixed weight of 1.0, but that will be very noisy. Scaling of the weights can be arbitrary, as long as it's consistent between all lights and surfaces.

### `RAB_GetLightTargetPdfForVolume`

`float RAB_GetLightTargetPdfForVolume(RAB_LightInfo light, float3 volumeCenter, float volumeRadius)`

Computes the weight of the given light for arbitrary surfaces located inside the specified volume. Used for world-space light grid construction (ReGIR).

### `RAB_SamplePolymorphicLight`

`RAB_LightSample RAB_SamplePolymorphicLight(RAB_LightInfo lightInfo, RAB_Surface surface, float2 uv)`

Samples a polymorphic light relative to the given receiver surface. For most light types, the "uv" parameter is just a pair of uniform random numbers, originally produced by the [`RAB_GetNextRandom`](#rab_getnextrandom) function and then stored in light reservoirs. For importance sampled environment lights, the "uv" parameter has the texture coordinates in the PDF texture, normalized to the (0..1) range.

### `RAB_GetConservativeVisibility`

`bool RAB_GetConservativeVisibility(RAB_Surface surface, RAB_LightSample lightSample)`

Traces a visibility ray that returns approximate, conservative visibility between the surface and the light sample. Conservative means if unsure, assume the light is visible. For example, conservative visibility can skip alpha-tested or translucent surfaces, which may provide significant performance gains. However, significant differences between this conservative visibility and the final one will result in more noise. 

This function is used in the spatial resampling functions for ray traced bias correction. The initial samples should also be kept or discarded based on visibility computed in the same way to keep the results unbiased.

### `RAB_GetTemporalConservativeVisibility`

`bool RAB_GetTemporalConservativeVisibility(RAB_Surface currentSurface, RAB_Surface previousSurface, RAB_LightSample lightSample)`

Same visibility ray tracing as [`RAB_GetConservativeVisibility`](#rab_getconservativevisibility) but for surfaces and light samples originating from the previous frame.

When the previous frame TLAS and BLAS data is available, the implementation should use that previous data and the `previousSurface` parameter. When the previous acceleration structures are not available, the implementation should use the `currentSurface` parameter, but that will make the results temporarily biased and, in some cases, more noisy. Specifically, the fused spatiotemporal resampling algorithm will produce very noisy results on animated objects.


## BRDF Sampling Related Functions

### `RAB_GetEnvironmentMapRandXYFromDir`

`float2 RAB_GetEnvironmentMapRandXYFromDir(float3 worldDir)`

Converts a world-space direction into a pair of numbers that, when passed into `RAB_SamplePolymorphicLight` for the environment light, will make a sample at the same direction.

### `RAB_EvaluateEnvironmentMapSamplingPdf`

`float RAB_EvaluateEnvironmentMapSamplingPdf(float3 L)`

Computes the probability of a particular direction being sampled from the environment map relative to all the other possible directions, based on the environment map PDF texture.

### `RAB_EvaluateLocalLightSourcePdf`

`float RAB_EvaluateLocalLightSourcePdf(uint lightIndex)`

Computes the probability of a particular light being sampled from the local light pool with importance sampling, based on the local light PDF texture.

### `RAB_GetSurfaceBrdfSample`

`bool RAB_GetSurfaceBrdfSample(RAB_Surface surface, inout RAB_RandomSamplerState rng, out float3 dir)`

Performs importance sampling of the surface's BRDF and returns the sampled direction.

### `RAB_GetSurfaceBrdfPdf`

`float RAB_GetSurfaceBrdfPdf(RAB_Surface surface, float3 dir)`

Computes the solid angle PDF of a particular direction being sampled by `RAB_GetSurfaceBrdfSample`.

### `RAB_GetLightDirDistance`

```
void RAB_GetLightDirDistance(RAB_Surface surface, RAB_LightSample lightSample,
    out float3 o_lightDir,
    out float o_lightDistance)
```

Returns the direction and distance from the surface to the light sample.

### `RAB_IsAnalyticLightSample`

`bool RAB_IsAnalyticLightSample(RAB_LightSample lightSample)`

Returns `true` if the light sample comes from an analytic light (e.g. a sphere or rectangle primitive) that cannot be sampled by BRDF rays.

### `RAB_LightSampleSolidAnglePdf`

`float RAB_LightSampleSolidAnglePdf(RAB_LightSample lightSample)`

Returns the solid angle PDF of the light sample.

### `RAB_TraceRayForLocalLight`

```
bool RAB_TraceRayForLocalLight(float3 origin, float3 direction, float tMin, float tMax,
    out uint o_lightIndex, out float2 o_randXY)
```

Traces a ray with the given parameters, looking for a light. If a local light is found, returns `true` and fills the output parameters with the light sample information. If a non-light scene object is hit, returns `true` and `o_lightIndex` is set to `RTXDI_InvalidLightIndex`. If nothing is hit, returns `false` and RTXDI will attempt to do environment map sampling.


## Misc Functions

### `RAB_GetNextRandom`

`float RAB_GetNextRandom(inout RAB_RandomSamplerState rng)`

Returns the next random number from the provided RNG state. The numbers must be between 0 and 1, so that for every returned `x`, `0.0 <= x < 1.0`. It is important to use a high-quality random number generator for RTXDI, otherwise various artifacts may appear. One example of such artifacts is when lighting results get "stuck" to a certain light in large parts of the screen, which is caused by insufficient variation in the random numbers.

### `RAB_AreMaterialsSimilar`

`bool RAB_AreMaterialsSimilar(RAB_Surface a, RAB_Surface b)`

Compares the materials of two surfaces, returns `true` if the surfaces are similar enough that it makes sense to share the light reservoirs between them. A conservative implementation would always return `true`, which might result in more noisy images than actually comparing the materials.

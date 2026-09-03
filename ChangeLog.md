# RTXDI SDK Change Log

## 3.1.0

**Release highlights:**

- Added DLSS Ray Reconstruction (DLSS-RR) support to the Full Sample, including a dedicated DLSS-RR input-formatting pass and denoiser-compatibility presets.
- Added ReSTIR PT features that keep the sample signal compatible with DLSS-RR (which prefers temporally independent samples), centered around stagnancy-driven decorrelation at final shading.
- Added compatibility-guided spatial neighbor selection (CGNS) to the Ultra mode of ReSTIR PT to improve reuse quality of high-frequency geometry like foliage.

**Breaking changes:**

- Renamed runtime and sample identifiers to follow RTX Kit conventions (camelCase variables and parameters, PascalCase functions), affecting many `RTXDI_*` function and struct names across ReSTIR DI, GI, and PT.
- Added `RTXDI_PTDecorrelationParameters` to `RTXDI_PTParameters` (as `decorrelation`) and `initialPathTracerPreservedBufferIndex` to `RTXDI_PTBufferIndices`. Constant-buffer layouts that copy these structs must be rebuilt.

**Updating from 3.0.0 to 3.1.0:**

1. Add `RTXDI_PTDecorrelationParameters` to the ReSTIR PT constant buffer as `RTXDI_PTParameters::decorrelation`. Host code can fill it with `rtxdi::GetDefaultReSTIRPTDecorrelationParams` and `ReSTIRPTContext::GetDecorrelationParameters` / `SetDecorrelationParameters`.
2. Include [`PT/Decorrelation.hlsli`](Libraries/Rtxdi/Include/Rtxdi/PT/Decorrelation.hlsli) in ReSTIR PT shaders that store duplication-map inputs, preserve the initial sample, detect fireflies, or apply decorrelation. Call `RTXDI_PTNeedsDuplicationInputs` before `RTXDI_PTStoreDuplicationInputs`, `RTXDI_PTNeedsPreservedInitialSample` when writing the extra reservoir slot, `RTXDI_PTDetectDecorrelationFireflies` from spatial resampling, and `RTXDI_PTApplyDecorrelation` from final shading. Host code can call `rtxdi::NeedsDuplicationMap`.
3. If using compatibility-guided spatial neighbor selection, implement `RAB_GetNeighborSelectionSurface` (see [`RAB_NeighborSelection.hlsli`](Samples/FullSample/Shaders/LightingPasses/RtxdiApplicationBridge/RAB_NeighborSelection.hlsli)), bind `RTXDI_SPATIAL_NEIGHBOR_SELECTION_BUFFER`, and call `RTXDI_PTSpatialNeighborSelection` before spatial resampling.

**Fixed issues:**

- Fixed transparent (glass) materials showing through occluding alpha-tested geometry such as foliage.
- Fixed Primary Surface Replacement (PSR) motion vectors for objects moving within mirror reflections.

**Misc improvements:**

- Added command-line options for ReSTIR PT quality presets and DLSS-RR configuration.
- Added a dedicated ReSTIR PT shader API reference ([ShaderAPI-RestirPT.md](Doc/ShaderAPI-RestirPT.md)) and expanded the ReSTIR PT and application-bridge documentation.
- Added `RTXDI_PTDecorrelationParameters` (`RTXDI_PTParameters::decorrelation`) and `PT/Decorrelation.hlsli` (`RTXDI_PTNeedsDuplicationMap`, `RTXDI_PTNeedsDuplicationInputs`, `RTXDI_PTNeedsPreservedInitialSample`, `RTXDI_PTDetectDecorrelationFireflies`, `RTXDI_PTApplyDecorrelation`) so applications can integrate final-shading decorrelation.
- Added [`RAB_NeighborSelection.hlsli`](Samples/FullSample/Shaders/LightingPasses/RtxdiApplicationBridge/RAB_NeighborSelection.hlsli) with the Full Sample `RAB_GetNeighborSelectionSurface` implementation used by compatibility-guided spatial neighbor selection.



## 3.0.0

**Release highlights:**

- Added ReSTIR PT resampling functions to the runtime and their integration in the Full Sample application.
- Added shader debug print for D3D12.
- Added path tracer visualization tool.
- Added Primary Surface Replacement (PSR) support for NRD integrations.
- Added visualization tools for DI, GI, and PT reservoirs.

**Breaking changes:**

- The RTXDI_PathTracerContext interface uses a primitive #define-based interface for maximum compatibility and requires an additional preprocessor macro.

**Fixed issues:**

- Removed ghosting issues that appeared on recent drivers.

**Misc improvements:**

- Updated Donut
- Updated NRD
- DLSS integration replaced by Donut's DLSS integration
- Added NVAPI for D3D12



## 2.3.0

**Release highlights:**

- Better folder organization: The samples now live in `Samples/`, external libraries now live in `External/`, and the test suite lives in `External/Tests/`.
- All folders and files now use the PascalCase naming convention.
- `RtxdiApplicationBridge.hlsli` broken down into constituent files based on functionality.
- `DIResamplingFunctions.hlsli` and `GIResamplingFunctions.hlsli` broken down into constituent files based on functionality.

**Breaking changes:**

- rtxdi-runtime folder moved to Libraries/Rtxdi, which is now organized into folders based on algorithm.
- File breakdowns mentioned above.

**Misc improvements:**

- Donut updated to latest version to include Blackwell support.



## 2.2.0

**Release highlights:**

- Updated [NRD](https://github.com/NVIDIAGameWorks/RayTracingDenoiser) to v4.4.2 and updated sample app code accordingly.
- Updated [Donut](https://github.com/NVIDIAGameWorks/donut) sample framework to support NRD update.
- Removed Packman hosting of dependencies and the corresponding "update_dependencies" scripts.
- DXC redistributable is now installed by CMake.
- Assets are now installed from the new [RTXDI Assets]() submodule.

**Breaking changes:**

- rtxdi-sdk folder moved into the [RTXDI Runtime](https://gitlab-master.nvidia.com/rtx/rtxdi-runtime) submodule.

**Fixed issues:**

- Fixed visual "sparkling" regression in fused resampling kernel.
- Fixed Linux build.

**Misc improvements:**

- Moved to [DXC v1.7.2212](https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.7.2212).



## 2.1.0

**Release highlights:**

- Major refactor of C++ API that separates the ReSTIR DI, ReGIR, and ReSTIR GI algorithms into distinct contexts.
- Shader functions and structs renamed to reflect partition of algorithms.
- Added `ImportanceSamplingContext` class that collects all 3 algorithms into a central class to ensure shared state is properly managed.
- UI redone for the rtxdi-sample project's ReSTIR DI, ReGIR, and ReSTIR GI sections.

**Breaking changes:**

- C++ API changed completely. Algorithms are now organized by a single context each.
- ReSTIR DI reservoir struct renamed from RTXDI_Reservoir to RTXDI_DIReservoir, with corresponding changes to DI reservoir functions.
- ReSTIR DI resampling functions renamed to have a DI infix to distinguish them from their GI counterparts.
- Shader headers broken down into more files.

**Fixed issues:**

- Fixed SPIR-V compiler issues.
- Reorganized UI to reflect algorithm/code path.

**Misc improvements:**

- Decoupled boiling filter, bias correction, and several other settings for ReSTIR DI and ReSTIR GI.
- Broke down ReSTIR DI local light sampling code into smaller functions for easier reuse and expansion.



## 2.0.0

**Release highlights:**

- Added ReSTIR GI resampling functions and their integration in the sample application.
- Added debug visualization for intermediate render targets.
- Added the `sdk-only` branch to make using RTXDI as a submodule easier.

**Breaking changes:**

- Added a required bridge function `RAB_ClampSamplePositionIntoView`.
- Changed the PDF texture requirements to include a full mip chain up to 1x1 pixels.

**Fixed issues:**

- Fixed bias that could be observed when using BRDF MIS rays.
- Fixed the checkerboard rendering mode when the ReLAX denoiser is used.
- Fixed a compile error when Vulkan is not enabled in CMake configuration.
- Fixed the fused spatio-temporal direct light resampling path on DX12.

**Misc improvements:**

- Updated [RTXGI](https://developer.nvidia.com/rtxgi) to v1.2.12 and switched to using an SDK-only branch of RTXDI.
- Updated [NRD](https://github.com/NVIDIAGameWorks/RayTracingDenoiser) to version 3.3.1.



## 1.3.0

**Release highlights:**

- Added Pairwise Multiple Importance Sampling (MIS) normalization for ReSTIR that improves resampling quality on glossy surfaces.
- Added MIS with BRDF rays. Light samples discovered from BRDF rays are mixed into ReSTIR, greatly improving image quality on low-roughness surfaces.
- Updated [NRD](https://github.com/NVIDIAGameWorks/RayTracingDenoiser) to version 3.0.

**Breaking changes:**

- Some bridge functions have changed signatures.
- Added required bridge functions for BRDF sampling.

**Misc improvements:**

- Removed the old "ReSTIR Direct + BRDF MIS" rendering mode, replaced by the new MIS mode.



## 1.2.1

**Breaking changes:**

- Added support for compiling the RTXDI shader headers in a GLSL environment. Some functions have changed their signatures to remove resource-type parameters, which are now passed through macros.

**Fixed issues:**

- Fixed the remaining [Slang](https://github.com/shader-slang/slang) compatibility issue in the RTXDI shader headers.

**New features:**

- Added various command line options for automated functional and performance testing.
- Added a minimal sample app to demonstrate a basic mesh lighting setup.

**Misc improvements:**

- Updated [NRD](https://github.com/NVIDIAGameWorks/RayTracingDenoiser) to version 2.10.



## 1.2.0

**Release highlights:**

- Integrated [RTXGI](https://developer.nvidia.com/rtxgi) into the sample app. The RTXGI probes provide indirect lighting for  primary or secondary surfaces, depending on the settings.
- Integrated [DLSS](https://developer.nvidia.com/dlss) into the sample app.
- Updated [NRD](https://github.com/NVIDIAGameWorks/RayTracingDenoiser) to version 2.9.
- Added support for rendering a denoiser confidence signal from ReSTIR, see [Computing Denoiser Confidence Inputs using RTXDI](doc/Confidence.md).
- Added sample permutations into the temporal resampling functions. These permutations greatly improve the temporal properties of the ReSTIR signal. See `enablePermutationSampling`.

**New features:**

- Added support for low-resolution rendering and temporal upscaling through TAAU or DLSS.
- Added resampling from the G-buffer on secondary surfaces to improve the secondary direct lighting sampling quality.
- Added an option to mix some noise back into the denoiser output, which improves final image sharpness with DLSS.
- Added custom visualization modes for many HDR signals like lighting luminance or reservoir weights.
- Added support for zero-radius point lights.

**API changes:**

- Added the `temporalSamplePixelPos` out parameter to the temporal and spatio-temporal resampling functions.
- Added the optional `selectedLightSample` in-out parameter to all resampling functions.

**Misc improvements:**

- Added a `-debug` command line option to enable the validation layers.
- Changed the motion vector formulation to be compatible with NRD and DLSS in case of dynamic resolution.
- Improved specular MIS with importance sampled environment maps.
- Improved temporal resampling performance by skipping the visibility ray for most pixels with no quality loss. See `enableVisibilityShortcut`.
- Improved the organization of the source code files into folders.
- Improved the processing of profiler readbacks to remove a pipeline bubble and improve overall performance.
- Made the RTXDI SDK shader headers compatible with the [Slang](https://github.com/shader-slang/slang) compiler.
- Moved the shading of secondary surfaces into a separate pass, which often improves performance.
- Re-designed the sample application user interface.

**Fixed issues:**

- Fixed the brightening bias that happened in disoccluded regions in the spatial resampling pass.
- Fixed the handling of reservoirs for lights that have disappeared.
- Fixed the data corruption in packed reservoirs that was caused by negative distance values.
- Fixed some issues with glTF models that have multiple geometries in their meshes.
- Fixed the `packman` file permissions on Linux.



## 1.1.0

**API changes:**

- Added the `RAB_GetTemporalConservativeVisibility` bridge function.
- Removed the `usePreviousFrameScene` parameter from the `RAB_GetConservativeVisibility` bridge function.

**Major code changes:**

- Switched to use the open source version of the [Donut framework](https://github.com/NVIDIAGameWorks/donut) and [glTF](https://www.khronos.org/gltf) models.
- Updated [NRD](https://github.com/NVIDIAGameWorks/RayTracingDenoiser) to version 2.5 that is now integrated as a git submodule and works on Linux (both x64 and ARM64 architectures).

**New features:**

- Added bloom post-processing effect.
- Added compatibility with the ARM64 processor architecture (when running Linux)
- Added support for skeletal animation in the sample application.
- Enabled the `KHR_ray_tracing_pipeline` API on Vulkan.
- Enabled TLAS updates (instead of rebuilds) in the sample application.

**Misc improvements:**

- Added dependency packages and scripts to make the sample application easy to build on Linux.
- Fixed some issues with the material model and BRDF evaluation.
- Improved the BRDF ray importance sampling logic.



## 1.0.0

Initial release.
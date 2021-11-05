# RTXDI SDK Change Log

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

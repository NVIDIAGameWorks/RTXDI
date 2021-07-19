# RTXDI SDK Change Log

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

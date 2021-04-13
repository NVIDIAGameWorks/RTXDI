
# RTXDI SDK and Sample App

## Introduction

**RTX** **D**irect **I**llumination is a framework that facilitates the implementations of efficient direct light sampling in real-time renderers. It is based on the **ReSTIR** algorithm published in the paper called "Spatiotemporal reservoir resampling for real-time ray tracing with dynamic direct lighting" by B. Bitterli et al.

For more information, see the [NVIDIA Developer Page](https://developer.nvidia.com/rtxdi).

## Package Contents

[`rtxdi-sdk`](rtxdi-sdk) contains the SDK source code files that are meant to be included into the application build:

- [`rtxdi-sdk/include`](rtxdi-sdk/include) has the include files, both for host code and for shaders
- [`rtxdi-sdk/include/rtxdi/ResamplingFunctions.hlsli`](rtxdi-sdk/include/rtxdi/ResamplingFunctions.hlsli) is the main shader include file that contains the resampling implementation
- [`rtxdi-sdk/shaders`](rtxdi-sdk/shaders) has the shader files that are supposed to be compiled through whatever means the application normally uses
- [`rtxdi-sdk/src`](rtxdi-sdk/src) has the host code with various utility functions for setting up the parameters and resources for resampling

[`src`](src) contains the sample application host code

[`shaders`](shaders) contains the sample application shaders

Additional contents delivered through packman:

`donut-snapshot` is a snapshot of the source code for the NVIDIA "Donut" rendering framework that the sample application is built on, and its dependencies.

`dxc` is a recent version of DirectX Shader Compiler;

`media` contains the media files necessary for the sample app to run.

## Building and Running the Sample App

### Setting up the source code tree and dependencies

1. Clone the repository.
2. Pull the media files and other large dependencies from packman by running `update_dependencies.bat`.

### Building and integrating NRD (optional)

To build the sample app with NRD (NVIDIA Real-Time Denoiser) integration, follow these steps to build and install NRD *before* building RTXDI SDK:

1. Get the NRD 2.0 source code from the [GitHub repository](https://github.com/NVIDIAGameWorks/RayTracingDenoiser/) (accessing the repository requires approval through the [NVIDIA Developer website](https://developer.nvidia.com/nvidia-rt-denoiser)).
2. Build NRD using the following batch files in its directory: `1-Deploy.bat`, `2-Build.bat`, `4b-Prepare NRD SDK.bat`. That will put all the necessary SDK files into the `_NRD_SDK` folder.
3. Copy that `_NRD_SDK` folder into the RTXDI SDK folder, next to `src`, and rename it to `NRD`. Then CMake should find the NRD SDK paths automatically. Alternatively, specify all the paths manually using the `NRD_INCLUDE_DIR`, `NRD_LIB_DIR`, and `NRD_SHADERS_DIR` CMake variables.

### Configuring and building the solution with CMake

The easiest option is to use [CMake GUI](https://cmake.org/download/).

1. Assuming that the RTXDI SDK tree is located in `D:\RTXDI`, set the following parameters in the GUI:
	- "Where is the source code" to `D:\RTXDI`
	- "Where to build the binaries" to `D:\RTXDI\build`
2. Click "Configure", set "Generator" to the Visual Studio you're using (tested with VS 2019 version 16.8.2), set "Optional platform" to x64, click "Finish".
4. Click "Generate", then "Open Project".
5. Build the solution with Visual Studio 
6. Run the `rtxdi-sample` project.

### Vulkan support

The RTXDI sample application can run using D3D12 or Vulkan, which is achieved through the NVRHI rendering API abstraction layer and HLSL shader compilation to SPIR-V through DXC (DirectX Shader Compiler). Unfortunately, the current version of DXC shipping with Vulkan SDK cannot compile the RTXDI SDK shaders into SPIR-V. For this reason, we deliver a compatible version of DXC through packman. If you wish to use a different (e.g. newer) version of DXC, it can be obtained from [Microsoft/DirectXShaderCompiler](https://github.com/Microsoft/DirectXShaderCompiler) on GitHub. The path to a custom version of DXC can be configured using the `DXC_DXIL_EXECUTABLE` and `DXC_SPIRV_EXECUTABLE` CMake variables.

By default, the sample app will run using D3D12. To start it in Vulkan mode, add `-vk` to the command line. To compile the sample app without Vulkan support, set the CMake variable `DONUT_WITH_VULKAN` to `OFF` and re-generate the project.

## Integration

See the [Integration Guide](doc/Integration.md).

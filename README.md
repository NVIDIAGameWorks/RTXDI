
# RTXDI SDK and Sample Applications

Version 2.2.0.

[Change Log](ChangeLog.md)

## Introduction

**RTX** **D**ynamic **I**llumination is a framework that facilitates the implementations of efficient direct light sampling in real-time renderers. It is based on the **ReSTIR** algorithm published in the paper called "Spatiotemporal reservoir resampling for real-time ray tracing with dynamic direct lighting" by B. Bitterli et al.

Starting with version 2.0, RTXDI also includes **ReSTIR GI** functionality, which allows applications to apply importance resampling to indirect illumination rendered using path tracing. For more information about the indirect illumination algorithm, see the paper called "ReSTIR GI: Path Resampling for Real-Time Path Tracing" by Y. Ouyang et al. The feature is described in more detail in [this document](doc/RestirGI.md).

For more information about RTXDI, see the [NVIDIA Developer Page](https://developer.nvidia.com/rtxdi).

## Package Contents

[`rtxdi-runtime`](https://github.com/NVIDIAGameWorks/rtxdi-runtime) is a submodule that contains the integrable runtime sources that are meant to be included in the application build:

- [`rtxdi-runtime/include/rtxdi`](https://github.com/NVIDIAGameWorks/rtxdi-runtime/tree/main/include/rtxdi) has the include files, both for host code and for shaders
- [`rtxdi-runtime/include/rtxdi/DIResamplingFunctions.hlsli`](https://github.com/NVIDIAGameWorks/rtxdi-runtime/tree/main/include/rtxdi/DIResamplingFunctions.hlsli) and [`rtxdi-runtime/include/rtxdi/GIResamplingFunctions.hlsli`](https://github.com/NVIDIAGameWorks/rtxdi-runtime/tree/main/include/rtxdi/GIResamplingFunctions.hlsli) are the main shader includes that contain the resampling implementations
- [`rtxdi-runtime/src`](https://github.com/NVIDIAGameWorks/rtxdi-runtime/tree/main/src) has the host code with various utility functions for setting up the parameters and resources for resampling

[`src`](src) contains the sample application host code.

[`shaders`](shaders) contains the sample application shaders.

[`donut`](donut) is a submodule structure with the ["Donut" rendering framework](https://github.com/NVIDIAGameWorks/donut) used to build the sample apps.

[`NRD`](NRD) is a submodule with the ["NRD" denoiser library](https://github.com/NVIDIAGameWorks/RayTracingDenoiser).

[`DLSS`](DLSS) is a submodule with the [Deep Learning Super-Sampling SDK](https://github.com/NVIDIA/DLSS).

[`rtxdi-assets`](https://github.com/NVIDIAGameWorks/rtxdi-assets) is a submodule containing the [RTXDI SDK Sample Assets](https://github.com/NVIDIAGameWorks/rtxdi-assets).

Additional content delivered during CMake configuration:

`thirdparty/dxc` is a recent version of DirectX Shader Compiler.

## Building and Running the Sample Apps

**Note** that because the [`rtxdi-assets`](https://github.com/NVIDIAGameWorks/rtxdi-assets) submodule uses LFS. Cloning it without [LFS](https://git-lfs.com) installed will result in files containing LFS pointers instead of the actual assets.

### Windows

1. Install LFS support by following the instructions on [git-lfs.com](https://git-lfs.com)

2. Clone the repository with all submodules:
	- `git clone --recursive https://github.com/NVIDIAGameWorks/RTXDI.git`

	If the clone was made non-recursively and the submodules are missing, clone them separately:

	- `git submodule update --init --recursive`

3. Configure the solution with CMake. The easiest option is to use [CMake GUI](https://cmake.org/download/).

4. Assuming that the RTXDI SDK tree is located in `D:\RTXDI`, set the following parameters in the GUI:
	- "Where is the source code" to `D:\RTXDI`
	- "Where to build the binaries" to `D:\RTXDI\build`

5. Click "Configure", set "Generator" to the Visual Studio you're using (tested with VS 2019 version 16.8.2), set "Optional platform" to x64, click "Finish".

6. Click "Generate", then "Open Project".

7. Build the solution with Visual Studio 

8. Run the `rtxdi-sample` or `minimal-sample` projects.

### Linux

1. Make sure the necessary build packages are installed on the target system. For Ubuntu 20.04 (amd64), the following command is sufficient:
	- `sudo apt install build-essential cmake xorg-dev libtinfo5`

2. Install LFS support by following the instructions on [git-lfs.com](https://git-lfs.com). Note that on Ubunutu 21.04+ you can simply:
	- `sudo apt install git-lfs`

3. Clone the repository with all submodules:
	- `git clone --recursive https://github.com/NVIDIAGameWorks/RTXDI.git`

	If the clone was made non-recursively and the submodules are missing, clone them separately:

	- `git submodule update --init --recursive`

4. Create a build folder:
	- `mkdir build && cd build`

5. Configure the project with CMake:
	- `cmake ..`

6. Build:
	- `make -j8` (example for an 8-core CPU, or use [Ninja](https://ninja-build.org) instead)

7. Run:
	- `bin/rtxdi-sample` or `bin/minimal-sample`

### Vulkan support

The RTXDI sample applications can run using D3D12 or Vulkan, which is achieved through the [NVRHI](https://github.com/NVIDIAGameWorks/nvrhi) rendering API abstraction layer and HLSL shader compilation to SPIR-V through DXC (DirectX Shader Compiler). We deliver a compatible version of DXC through packman. If you wish to use a different (e.g. newer) version of DXC, it can be obtained from [Microsoft/DirectXShaderCompiler](https://github.com/Microsoft/DirectXShaderCompiler) on GitHub. The path to a custom version of DXC can be configured using the `DXC_PATH` and `DXC_SPIRV_PATH` CMake variables.

By default, the sample apps will run using D3D12 on Windows. To start them in Vulkan mode, add `--vk` to the command line. To compile the sample apps without Vulkan support, set the CMake variable `DONUT_WITH_VULKAN` to `OFF` and re-generate the project.

To enable SPIV-V compileation tests, set the `GLSLANG_PATH` variable in CMake to the path to glslangValidator.exe in your Vulkan installation.

## Integration

See the [Integration Guide](doc/Integration.md).

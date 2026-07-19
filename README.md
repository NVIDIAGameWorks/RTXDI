
# RTXDI SDK and Sample Applications

Version 3.0.0

[Change Log](ChangeLog.md)

## Introduction

**RTX** **D**ynamic **I**llumination is a framework that originally facilitated implementation of efficient direct light sampling in real-time renderers. It is based on the **ReSTIR** algorithm published in the paper called "Spatiotemporal reservoir resampling for real-time ray tracing with dynamic direct lighting" by B. Bitterli et al.

Version 3.0 of RTXDI introduces **ReSTIR PT**, which allows applications to apply importance resampling to all indirect lighting rendered using path tracing. For more information about the ReSTIR PT algorithm, see the paper "Generalized Resampled Importance Sampling: Foundations of ReSTIR" by Lin et al. The implementation is described in more detail in [this document](Doc/RestirPT.md).

Version 2.0 of RTXDI introduced **ReSTIR GI**, which allows applications to apply importance resampling to indirect diffuse illumination rendered using path tracing. For more information about the ReSTIR GI algorithm, see the paper called "ReSTIR GI: Path Resampling for Real-Time Path Tracing" by Y. Ouyang et al. The feature is described in more detail in [this document](Doc/RestirGI.md).

For more information about RTXDI, see the [NVIDIA Developer Page](https://developer.nvidia.com/rtxdi).

For an academic presentation on the ReSTIR family of algorithms, see the SIGGRAPH 2023 course [A Gentle Introduction to ReSTIR](https://intro-to-restir.cwyman.org/).

## Package Contents

[`Libraries/Rtxdi`](https://github.com/NVIDIA-RTX/RTXDI-Library) is a submodule that contains the integrable runtime sources that are meant to be included in the application build:

- [`Rtxdi/Include/Rtxdi`](https://github.com/NVIDIA-RTX/RTXDI-Library/tree/main/Include/Rtxdi) has the include files, both for host code and for shaders.
- [`Rtxdi/Include/Rtxdi/DI/`](https://github.com/NVIDIA-RTX/RTXDI-Library/tree/main/Include/Rtxdi/DI), [`Rtxdi/Include/Rtxdi/GI/`](https://github.com/NVIDIA-RTX/RTXDI-Library/tree/main/Include/Rtxdi/GI), and [`Rtxdi/Include/Rtxdi/PT/`](https://github.com/NVIDIA-RTX/RTXDI-Library/tree/main/Include/Rtxdi/PT) are the main shader include folders that contain the resampling implementations for their respective algorithms.
- [`Rtxdi/Source`](https://github.com/NVIDIA-RTX/RTXDI-Library/tree/main/Source) has the host code with various utility functions for setting up the parameters and resources for resampling.

[`Samples`](Samples) contains two sample projects, [`MinimalSample`](Samples/MinimalSample) and [`FullSample`](Samples/FullSample). The [`MinimalSample/Source`](Samples/MinimalSample/Source) project implements ReSTIR DI in a single combined pass to show a minimum viable implementation. The [`FullSample/Source`](Samples/FullSample/Source) project implements ReSTIR DI in several passes integrated into a broader rendering pipeline to show a more standard implementation. The shaders for each project live in their respective [`MinimalSample/Shaders`](Samples/MinimalSample/Shaders) and [`FullSample/Shaders`](Samples/FullSample/Shaders) folders.

[`External`](External) contains project dependencies both from Nvidia and third parties:

- `External/donut` is a Git submodule structure with the ["Donut" rendering framework](https://github.com/NVIDIA-RTX/Donut) used to build the sample apps.

- `External/NRD` is a Git submodule with the ["NRD" denoiser library](https://github.com/NVIDIA-RTX/NRD).

- `External/DLSS` is a Git submodule with the [Deep Learning Super-Sampling SDK](https://github.com/NVIDIA/DLSS).

- `External/dxc` is a recent version of DirectX Shader Compiler. However, unlike the other dependencies, it is not a Git submodule but is instead fetched by CMake at project configuration time.

`Assets/Media` is a Git submodule containing the [RTXDI SDK Sample Assets](https://github.com/NVIDIA-RTX/RTXDI-Assets).

## Known Issues

- The Vulkan validation layer emits a warning about the `t_NeighborOffsets` buffer not aligning with the resource that's given to it by the application.
- The `RTXDI_PathTracerContext` is currently implemented with a more primitive `#define`-based interface for compatibility reasons.
- ReSTIR PT's spatial pass has some bias to it.
- Debug path visualization sometimes adds an extra vertex at the camera.
- In rare cases, shader debug print can cause unexpected correctness issues if used excessively.

## Building and Running the Sample Apps

**Note** that because the [`rtxdi-assets`](https://github.com/NVIDIA-RTX/RTXDI-Assets) submodule that is cloned into `Assets/Media` uses LFS, cloning it without [LFS](https://git-lfs.com) installed will result in files containing LFS pointers instead of the actual assets.

### Windows

1. Install LFS support by following the instructions on [git-lfs.com](https://git-lfs.com)

2. Clone the repository with all submodules:
	- `git clone --recursive https://github.com/NVIDIA-RTX/RTXDI.git`

	If the clone was made non-recursively and the submodules are missing, clone them separately:

	- `git submodule update --init --recursive`

3. Configure the solution with CMake. The easiest option is to use [CMake GUI](https://cmake.org/download/).

4. Assuming that the RTXDI SDK tree is located in `D:\RTXDI`, set the following parameters in the GUI:
	- "Where is the source code" to `D:\RTXDI`
	- "Where to build the binaries" to `D:\RTXDI\build`

5. Click "Configure", set "Generator" to the Visual Studio you're using (tested with VS 2019 version 16.8.2), set "Optional platform" to x64, click "Finish".

6. Click "Generate", then "Open Project".

7. Build the solution with Visual Studio 

8. Run the `FullSample` or `MinimalSample` projects.

### Linux

1. Make sure the necessary build packages are installed on the target system. For Ubuntu 20.04 (amd64), the following command is sufficient:
	- `sudo apt install build-essential cmake xorg-dev libtinfo5`

2. Install LFS support by following the instructions on [git-lfs.com](https://git-lfs.com). Note that on Ubunutu 21.04+ you can simply:
	- `sudo apt install git-lfs`

3. Clone the repository with all submodules:
	- `git clone --recursive https://github.com/NVIDIA-RTX/RTXDI.git`

	If the clone was made non-recursively and the submodules are missing, clone them separately:

	- `git submodule update --init --recursive`

4. Create a build folder:
	- `mkdir build && cd build`

5. Configure the project with CMake:
	- `cmake .. -DNVRHI_WITH_NVAPI=OFF`

6. Build:
	- `make -j8` (example for an 8-core CPU, or use [Ninja](https://ninja-build.org) instead)

7. Run:
	- `bin/FullSample` or `bin/MinimalSample`

### Vulkan support

The RTXDI sample applications can run using D3D12 or Vulkan, which is achieved through the [NVRHI](https://github.com/NVIDIA-RTX/NVRHI) rendering API abstraction layer and HLSL shader compilation to SPIR-V through DXC (DirectX Shader Compiler). We deliver a compatible version of DXC through packman. If you wish to use a different (e.g. newer) version of DXC, it can be obtained from [Microsoft/DirectXShaderCompiler](https://github.com/Microsoft/DirectXShaderCompiler) on GitHub. The path to a custom version of DXC can be configured using the `DXC_PATH` and `DXC_SPIRV_PATH` CMake variables.

By default, the sample apps will run using D3D12 on Windows. To start them in Vulkan mode, add `--device.vk` to the command line. To compile the sample apps without Vulkan support, set the CMake variable `DONUT_WITH_VULKAN` to `OFF` and re-generate the project.

To enable SPIV-V compilation tests, set the `GLSLANG_PATH` variable in CMake to the path to glslangValidator.exe in your Vulkan installation.

## Integration

See the [Integration Guide](Doc/Integration.md).

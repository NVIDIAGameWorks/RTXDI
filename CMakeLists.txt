cmake_minimum_required(VERSION 3.12)

project(RTXDI)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

if (MSVC)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W3 /MP")

	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
	set(CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL} /MT")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
	set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /MT")

	set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /MTd")
	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MT")
	set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} /MT")
	set(CMAKE_C_FLAGS_MINSIZEREL "${CMAKE_C_FLAGS_MINSIZEREL} /MT")
endif()

option(DONUT_WITH_ASSIMP "" OFF)
option(DONUT_USE_DXIL_ON_DX12 "" ON)
option(DONUT_WITH_DX11 "" OFF)
option(DONUT_WITH_VULKAN "" ON)
option(DONUT_WITH_SQLITE "" OFF)
option(DONUT_WITH_LZ4 "" OFF)
option(NVRHI_WITH_DXR "" ON)

if (WIN32)
	# Point to the DXC provided through packman
	set(DXC_DXIL_EXECUTABLE "${CMAKE_CURRENT_LIST_DIR}/dxc/dxc.exe" CACHE STRING 
		"Path to DirectX Shader Compiler for DXIL output")
	set(DXC_SPIRV_EXECUTABLE "${CMAKE_CURRENT_LIST_DIR}/dxc/dxc.exe" CACHE STRING 
		"Path to DirectX Shader Compiler for SPIR-V output")
endif()

include(NRD.cmake)

set(DONUT_SHADERS_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/bin/shaders/framework")

if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/donut")
	add_subdirectory(donut)
	set(DONUT_PATH "${CMAKE_CURRENT_LIST_DIR}/donut")
else()
	add_subdirectory(donut-snapshot)
	set(DONUT_PATH "${CMAKE_CURRENT_LIST_DIR}/donut-snapshot")
endif()

add_subdirectory(rtxdi-sdk)
add_subdirectory(shaders)
add_subdirectory(src)

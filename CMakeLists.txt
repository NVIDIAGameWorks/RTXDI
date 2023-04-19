cmake_minimum_required(VERSION 3.10)

project(RTXDI)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

if (MSVC)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W3 /MP")

    macro(replace_msvcrt var value)
        # Remove the existing /MD-type flags, if any
        string(REGEX REPLACE "/M[TD]d?\\s*" "" ${var} ${${var}})
        # Append the new flag
        set(${var} "${${var}} ${value}")
    endmacro(replace_msvcrt)
    
    replace_msvcrt(CMAKE_C_FLAGS_DEBUG "/MTd")
    replace_msvcrt(CMAKE_C_FLAGS_MINSIZEREL "/MT")
    replace_msvcrt(CMAKE_C_FLAGS_RELEASE "/MT")
    replace_msvcrt(CMAKE_C_FLAGS_RELWITHDEBINFO "/MT")

    replace_msvcrt(CMAKE_CXX_FLAGS_DEBUG "/MTd")
    replace_msvcrt(CMAKE_CXX_FLAGS_MINSIZEREL "/MT")
    replace_msvcrt(CMAKE_CXX_FLAGS_RELEASE "/MT")
    replace_msvcrt(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/MT")

elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")

	# JsonCpp uses its own deprecated methods, which produces annoying warnings.
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-declarations")

endif()

option(DONUT_WITH_DX11 "" OFF)
option(DONUT_WITH_VULKAN "" ON)
option(DONUT_WITH_LZ4 "" OFF)
option(DONUT_WITH_MINIZ "" OFF)


# Point to the DXC provided through packman
if (WIN32)
	set(PACKMAN_DXC "${CMAKE_CURRENT_LIST_DIR}/dxc/bin/x64/dxc.exe")
else()
	set(PACKMAN_DXC "${CMAKE_CURRENT_LIST_DIR}/dxc/bin/dxc")
endif()
if (EXISTS "${PACKMAN_DXC}")
	if (WIN32 AND NOT DXC_PATH)
		set(DXC_PATH "${PACKMAN_DXC}" CACHE STRING "Path to DirectX Shader Compiler for DXIL output")
	endif()
	if (NOT DXC_SPIRV_PATH)
		set(DXC_SPIRV_PATH "${PACKMAN_DXC}" CACHE STRING "Path to DirectX Shader Compiler for SPIR-V output")
	endif()
endif()
# We have our own compiler here, don't let ShaderMake search for it (and potentially fail)
set(SHADERMAKE_SEARCH_FOR_COMPILERS OFF CACHE BOOL "")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
set(DONUT_SHADERS_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/shaders/framework")

add_subdirectory(donut)
set(DONUT_PATH "${CMAKE_CURRENT_LIST_DIR}/donut")

include(NRD.cmake)
include(DLSS.cmake)
include(rtxdi-runtime.cmake)

set(GLSLANG_PATH "" CACHE STRING "Path to glslangValidator for GLSL header verification (optional)")

# Temporarily use cxxopts from NVRHI
if (NOT TARGET cxxopts)
	option(CXXOPTS_BUILD_EXAMPLES OFF)
	option(CXXOPTS_BUILD_TESTS OFF)
	option(CXXOPTS_ENABLE_INSTALL OFF)
	add_subdirectory(donut/nvrhi/thirdparty/cxxopts)
endif()

add_subdirectory(shaders)
add_subdirectory(src)
add_subdirectory(minimal/src)
add_subdirectory(minimal/shaders)

if (MSVC)
	set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT rtxdi-sample)
endif()

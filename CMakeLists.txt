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

option(DONUT_USE_DXIL_ON_DX12 "" ON)
option(DONUT_WITH_DX11 "" OFF)
option(DONUT_WITH_VULKAN "" ON)
option(DONUT_WITH_LZ4 "" OFF)
option(DONUT_WITH_MINIZ "" OFF)

if (WIN32)
	set(EXE_SUFFIX ".exe")
else()
	set(EXE_SUFFIX "")
endif()

# Point to the DXC provided through packman
set(PACKMAN_DXC "${CMAKE_CURRENT_LIST_DIR}/dxc/bin/dxc${EXE_SUFFIX}")
if (EXISTS "${PACKMAN_DXC}")
	if (WIN32 AND NOT DXC_DXIL_EXECUTABLE)
		set(DXC_DXIL_EXECUTABLE "${PACKMAN_DXC}" CACHE STRING "Path to DirectX Shader Compiler for DXIL output")
	endif()
	if (NOT DXC_SPIRV_EXECUTABLE)
		set(DXC_SPIRV_EXECUTABLE "${PACKMAN_DXC}" CACHE STRING "Path to DirectX Shader Compiler for SPIR-V output")
	endif()
endif()

set(DONUT_SHADERS_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/shaders/framework")

if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/donut")
	add_subdirectory(donut)
	set(DONUT_PATH "${CMAKE_CURRENT_LIST_DIR}/donut")
else()
	add_subdirectory(donut-snapshot)
	set(DONUT_PATH "${CMAKE_CURRENT_LIST_DIR}/donut-snapshot")
endif()

include(NRD.cmake)
include(DLSS.cmake)

if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/RTXGI/CMakeLists.txt")
	option(RTXDI_WITH_RTXGI "Enable the RTXGI integration" ON)

	if (RTXDI_WITH_RTXGI)
		option(RTXGI_DDGI_RESOURCE_MANAGEMENT "" ON)
		option(RTXGI_DDGI_USE_SHADER_CONFIG_FILE "" ON)

		if (UNIX)
			set(RTXGI_API_VULKAN_SDK ON)
			set(Vulkan_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/donut/nvrhi/thirdparty/Vulkan-Headers/include")
		endif()

		add_subdirectory(RTXGI)
	endif()
endif()

set(GLSLANG_EXECUTABLE "" CACHE STRING "Path to glslangValidator for GLSL header verification (optional)")

add_subdirectory(rtxdi-sdk)
add_subdirectory(shaders)
add_subdirectory(src)
add_subdirectory(minimal/src)
add_subdirectory(minimal/shaders)

if (MSVC)
	set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT rtxdi-sample)
endif()

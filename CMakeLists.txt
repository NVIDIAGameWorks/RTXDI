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

# Helper to download and unzip a package from a URL
# Uses a zero-length file to identify the version of the package
function(CheckAndDownloadPackage NAME VERSION LOCAL_PATH URL)
    # Do we already have the correct version?
    if(NOT EXISTS ${LOCAL_PATH}/${VERSION}.ver)
        # Was there a previous version that we need to delete?
        if(EXISTS ${LOCAL_PATH})
            message(STATUS "Deleting old " ${NAME})
            file(REMOVE_RECURSE ${LOCAL_PATH})
        endif()
        message(STATUS "Obtaining " ${NAME} " " ${VERSION})
        file(DOWNLOAD ${URL} ${LOCAL_PATH}.zip)
        message(STATUS "Extracting " ${NAME})
        file(ARCHIVE_EXTRACT INPUT ${LOCAL_PATH}.zip DESTINATION ${LOCAL_PATH})
        file(REMOVE ${LOCAL_PATH}.zip)
        # Create an empty file so we know which version we have
        file(WRITE ${LOCAL_PATH}/${VERSION}.ver)
    endif()
endfunction()

# Download DXC
if (WIN32)
	CheckAndDownloadPackage("DXC" "v1.7.2212.1" ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/dxc https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.7.2212.1/dxc_2023_03_01.zip)
	set(REDIST_DXC "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/dxc/bin/x64/dxc.exe")
else()
	CheckAndDownloadPackage("DXC" "v1.7.2212.1" ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/dxc https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.7.2212.1/linux_dxc_2023_03_01.x86_64.tar.gz)
	# Add execute permissions
	file(CHMOD ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/dxc/bin/dxc PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
	set(REDIST_DXC "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/dxc/bin/dxc")
endif()
if (EXISTS "${REDIST_DXC}")
	if (WIN32 AND NOT DXC_PATH)
		set(DXC_PATH "${REDIST_DXC}" CACHE STRING "Path to DirectX Shader Compiler for DXIL output")
	endif()
	if (NOT DXC_SPIRV_PATH)
		set(DXC_SPIRV_PATH "${REDIST_DXC}" CACHE STRING "Path to DirectX Shader Compiler for SPIR-V output")
	endif()
endif()
# Have ShaderMake use included DXC
option(SHADERMAKE_FIND_DXC "" OFF)
option(SHADERMAKE_FIND_DXC_SPIRV "" OFF)
# But do have ShaderMake find FXC for compiling NRD shaders
option(SHADERMAKE_FIND_FXC "" ON)

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

set(RTXDI_RUNTIME_INCLUDE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/rtxdi-runtime/include")

set(GLSLANG_PATH "" CACHE STRING "Path to glslangValidator for GLSL header verification (optional)")

if (NOT TARGET cxxopts)
	option(CXXOPTS_BUILD_EXAMPLES OFF)
	option(CXXOPTS_BUILD_TESTS OFF)
	option(CXXOPTS_ENABLE_INSTALL OFF)
	add_subdirectory(thirdparty/cxxopts)
endif()

add_subdirectory(shaders)
add_subdirectory(src)
add_subdirectory(minimal/src)
add_subdirectory(minimal/shaders)
add_subdirectory(rtxdi-runtime-shader-tests)

if (MSVC)
	set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT rtxdi-sample)
endif()

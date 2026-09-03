cmake_minimum_required(VERSION 3.10)

project(RTXDI)

include(CMakeDependentOption)

set(CMAKE_CXX_STANDARD 20)
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
option(DONUT_WITH_DLSS "" ON)

cmake_dependent_option(NVRHI_WITH_NVAPI "Include NVAPI support (requires NVAPI SDK)" ON "WIN32" OFF)
set(NVAPI_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/External/NVAPI/" CACHE STRING "Path to NVAPI include headers/shaders" )
if (WIN32)
    set(NVAPI_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/External/NVAPI/amd64/nvapi64.lib" CACHE STRING "Path to NVAPI .lib file")
endif()

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
	CheckAndDownloadPackage("DXC" "v1.8.2505.1" ${CMAKE_CURRENT_SOURCE_DIR}/External/dxc https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2505.1/dxc_2025_07_14.zip)
	set(SHADERMAKE_DXC_PATH "${CMAKE_CURRENT_SOURCE_DIR}/External/dxc/bin/x64/dxc.exe")
else()
	CheckAndDownloadPackage("DXC" "v1.8.2505.1" ${CMAKE_CURRENT_SOURCE_DIR}/External/dxc https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2505.1/linux_dxc_2025_07_14.x86_64.tar.gz)
	# Add execute permissions
	file(CHMOD ${CMAKE_CURRENT_SOURCE_DIR}/External/dxc/bin/dxc PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
	set(SHADERMAKE_DXC_PATH "${CMAKE_CURRENT_SOURCE_DIR}/External/dxc/bin/dxc")
endif()
if (EXISTS "${SHADERMAKE_DXC_PATH}")
	if (WIN32 AND NOT DXC_PATH)
		set(DXC_PATH "${SHADERMAKE_DXC_PATH}" CACHE STRING "Path to DirectX Shader Compiler for DXIL output")
	endif()
	if (NOT DXC_SPIRV_PATH)
		set(DXC_SPIRV_PATH "${SHADERMAKE_DXC_PATH}" CACHE STRING "Path to DirectX Shader Compiler for SPIR-V output")
	endif()
endif()
# Have ShaderMake use included DXC
option(SHADERMAKE_FIND_DXC "" OFF)
option(SHADERMAKE_FIND_DXC_SPIRV "" OFF)
# FXC is the Windows DXBC compiler used by NRD on Windows only.
cmake_dependent_option(SHADERMAKE_FIND_FXC "Find FXC in Windows SDK for NRD DXBC shaders" ON "WIN32" OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
set(DONUT_SHADERS_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/shaders/framework")

# Put the ShaderMake binary with the other build outputs
set(SHADERMAKE_BIN_OUTPUT_PATH "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}" CACHE STRING "")

add_subdirectory(External/donut)
set(DONUT_PATH "${CMAKE_CURRENT_LIST_DIR}/External/donut")

include(External/NRD.cmake)

if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/Libraries/Rtxdi/CMakeLists.txt")

    add_subdirectory(Libraries/Rtxdi)
    set(RTXDI_RUNTIME_INCLUDE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Rtxdi/Include")
    set_target_properties(Rtxdi PROPERTIES FOLDER "RTXDI SDK")

endif()

set(GLSLANG_PATH "" CACHE STRING "Path to glslangValidator for GLSL header verification (optional)")

if (NOT TARGET cxxopts)
	option(CXXOPTS_BUILD_EXAMPLES OFF)
	option(CXXOPTS_BUILD_TESTS OFF)
	option(CXXOPTS_ENABLE_INSTALL OFF)
	add_subdirectory(External/cxxopts)
endif()

add_subdirectory(Samples/FullSample/Shaders)
add_subdirectory(Samples/FullSample/Source)
add_subdirectory(Samples/MinimalSample/Shaders)
add_subdirectory(Samples/MinimalSample/Source)
add_subdirectory(Support/Tests/FullSampleStructAlignmentTests)
add_subdirectory(Support/Tests/RtxdiRuntimeShaderTests)
add_subdirectory(Support/Tests/RtxdiRuntimeStructAlignmentTests)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT FullSample)

if (MSVC)
	set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT FullSample)
endif()

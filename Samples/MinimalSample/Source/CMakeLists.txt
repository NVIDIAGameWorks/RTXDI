
file(GLOB sources "*.cpp" "*.h")

set(project MinimalSample)
set(folder "RTXDI SDK")

include(CMakeDependentOption)

add_executable(${project} WIN32 ${sources})

target_link_libraries(${project} donut_core donut_engine donut_app donut_render Rtxdi cxxopts)
add_dependencies(${project} MinimalSampleShaders)
set_target_properties(${project} PROPERTIES FOLDER ${folder})

if (UNIX AND NOT APPLE AND NOT NVRHI_BUILD_SHARED)
    if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.24")
        target_link_libraries(${project} "$<LINK_GROUP:RESCAN,nvrhi_vk,nvrhi>")
    else()
        target_link_libraries(${project} "-Wl,--start-group" nvrhi_vk nvrhi "-Wl,--end-group")
    endif()
endif()

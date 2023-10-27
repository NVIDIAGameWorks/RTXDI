
file(GLOB sources "*.cpp" "*.h")

set(project minimal-sample)
set(folder "RTXDI SDK")

include(CMakeDependentOption)

add_executable(${project} WIN32 ${sources})

target_link_libraries(${project} donut_core donut_engine donut_app donut_render rtxdi-runtime cxxopts)
add_dependencies(${project} minimal-sample-shaders)
set_target_properties(${project} PROPERTIES FOLDER ${folder})

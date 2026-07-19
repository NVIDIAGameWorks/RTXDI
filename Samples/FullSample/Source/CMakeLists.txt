set(project FullSample)
set(folder "RTXDI SDK")

set(sources
	"App/CommandLineArgs.h"
	"App/CommandLineArgs.cpp"
	"App/LoggingCallback.h"
	"App/LoggingCallback.cpp"
	"App/SceneRenderer.h"
	"App/SceneRenderer.cpp"
	"App/Screenshot.h"
	"App/Screenshot.cpp"
	"ShaderDebug/DebugVizPasses.cpp"
	"ShaderDebug/DebugVizPasses.h"
	"ShaderDebug/PackedDataVizPass.cpp"
	"ShaderDebug/PackedDataVizPass.h"
	"ShaderDebug/PTPathViz/PTPathVizLinesPass.cpp"
	"ShaderDebug/PTPathViz/PTPathVizLinesPass.h"
	"ShaderDebug/PTPathViz/PTPathVizPass.cpp"
	"ShaderDebug/PTPathViz/PTPathVizPass.h"
	"ShaderDebug/PTPathViz/PTPathVizPreprocessPass.cpp"
	"ShaderDebug/PTPathViz/PTPathVizPreprocessPass.h"
	"ShaderDebug/ReservoirSubfieldVizPasses/DIReservoirVizPass.cpp"
	"ShaderDebug/ReservoirSubfieldVizPasses/DIReservoirVizPass.h"
	"ShaderDebug/ReservoirSubfieldVizPasses/GIReservoirVizPass.cpp"
	"ShaderDebug/ReservoirSubfieldVizPasses/GIReservoirVizPass.h"
	"ShaderDebug/ReservoirSubfieldVizPasses/PTReservoirVizPass.cpp"
	"ShaderDebug/ReservoirSubfieldVizPasses/PTReservoirVizPass.h"
	"ShaderDebug/ShaderPrint.h"
	"ShaderDebug/ShaderPrint.cpp"
	"ShaderDebug/VisualizationPass.cpp"
	"ShaderDebug/VisualizationPass.h"
	"RenderPasses/DenoisingPasses/ConfidencePass.cpp"
	"RenderPasses/DenoisingPasses/ConfidencePass.h"
	"RenderPasses/DenoisingPasses/FilterGradientsPass.cpp"
	"RenderPasses/DenoisingPasses/FilterGradientsPass.h"
	"RenderPasses/DenoisingPasses/NrdIntegration.cpp"
	"RenderPasses/DenoisingPasses/NrdIntegration.h"
	"RenderPasses/LightingPasses/LightingPasses.cpp"
	"RenderPasses/LightingPasses/LightingPasses.h"
	"RenderPasses/LightingPasses/ReSTIRDIRenderPasses.h"
	"RenderPasses/LightingPasses/ReSTIRDIRenderPasses.cpp"
	"RenderPasses/LightingPasses/ReSTIRGIRenderPasses.h"
	"RenderPasses/LightingPasses/ReSTIRGIRenderPasses.cpp"
	"RenderPasses/LightingPasses/ReSTIRPTRenderPasses.h"
	"RenderPasses/LightingPasses/ReSTIRPTRenderPasses.cpp"
	"RenderPasses/AccumulationPass.cpp"
	"RenderPasses/AccumulationPass.h"
	"RenderPasses/CompositingPass.cpp"
	"RenderPasses/CompositingPass.h"
	"RenderPasses/GBufferPass.cpp"
	"RenderPasses/GBufferPass.h"
	"RenderPasses/GenerateMipsPass.cpp"
	"RenderPasses/GenerateMipsPass.h"
	"RenderPasses/GlassPass.cpp"
	"RenderPasses/GlassPass.h"
	"RenderPasses/PrepareLightsPass.cpp"
	"RenderPasses/PrepareLightsPass.h"
	"RenderPasses/RayTracingPass.cpp"
	"RenderPasses/RayTracingPass.h"
	"RenderPasses/RenderEnvironmentMapPass.cpp"
	"RenderPasses/RenderEnvironmentMapPass.h"
	"Scene/Lights.h"
	"Scene/Lights.cpp"
	"Scene/SampleMesh.h"
	"Scene/SampleScene.cpp"
	"Scene/SampleScene.h"
	"main.cpp"
	"Profiler.cpp"
	"Profiler.h"
	"ProfilerSections.h"
	"RenderTargets.cpp"
	"RenderTargets.h"
	"RtxdiResources.cpp"
	"RtxdiResources.h"
	"UserInterface.cpp"
	"UserInterface.h")

# Organize MSVS filters (the little folders in the solution explorer) to match the folder structure
foreach(source IN LISTS sources)
    get_filename_component(source_path "${source}" PATH)
    string(REPLACE "/" "\\" source_path_msvc "${source_path}")
    source_group("${source_path_msvc}" FILES "${source}")
endforeach()


include(CMakeDependentOption)

cmake_dependent_option(RTXDI_CONSOLE_APP "Build the sample as a console application" OFF WIN32 OFF)

if (RTXDI_CONSOLE_APP)
	add_executable(${project} ${sources})
	target_compile_definitions(${project} PRIVATE IS_CONSOLE_APP=1)
else()
	add_executable(${project} WIN32 ${sources})
endif()

target_link_libraries(${project} donut_core donut_engine donut_app donut_render Rtxdi cxxopts)
add_dependencies(${project} FullSampleShaders)
set_target_properties(${project} PROPERTIES FOLDER ${folder})
target_include_directories(${project} PRIVATE ${FULL_SAMPLE_SHARED_INCLUDE_PATH})
target_include_directories(${project} PRIVATE .)

if (TARGET NRD)
	target_compile_definitions(${project} PRIVATE WITH_NRD=1)
	target_link_libraries(${project} NRD)
	
	# NRD doesn't add a public include path at this time, work around that
	target_include_directories(${project} PRIVATE "${CMAKE_SOURCE_DIR}/NRD/Include")
endif()

if (DONUT_WITH_DLSS)
	add_custom_command(TARGET ${project} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${DLSS_SHARED_LIBRARY_PATH}"
		"$<TARGET_FILE_DIR:${project}>/")
endif()

if(DONUT_WITH_AFTERMATH)
	file(COPY ${AFTERMATH_RUNTIME_LIBRARY} DESTINATION ${CMAKE_BINARY_DIR}/bin)
endif()


if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/NRD")
	set(NRD_DXC_PATH ${DXC_DXIL_EXECUTABLE})
	set(NRD_DXC_SPIRV_PATH ${DXC_SPIRV_EXECUTABLE})
	option(NRD_STATIC_LIBRARY "" ON)

	add_subdirectory(NRD)
	
	set_target_properties(NRD PROPERTIES FOLDER NRD)
	set_target_properties(Shaders PROPERTIES FOLDER NRD)
	set_target_properties(CreateFolderForShaders PROPERTIES FOLDER NRD)
endif()

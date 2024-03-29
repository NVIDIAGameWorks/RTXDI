
if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/NRD/CMakeLists.txt")
	set(NRD_DXC_PATH ${DXC_PATH})
	set(NRD_DXC_SPIRV_PATH ${DXC_SPIRV_PATH})
	option(NRD_STATIC_LIBRARY "" ON)

	add_subdirectory(NRD)
	
	set_target_properties(NRD PROPERTIES FOLDER NRD)
	set_target_properties(NRDShaders PROPERTIES FOLDER NRD)
endif()

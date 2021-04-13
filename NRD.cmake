
find_path(NRD_INCLUDE_DIR 
	Nrd.h
	PATHS ${CMAKE_CURRENT_LIST_DIR}/NRD/Include)

if (WIN32)
	find_path(NRD_LIB_DIR
		NRD.lib
		PATHS ${CMAKE_CURRENT_LIST_DIR}/NRD/Lib/Release)
else()
	find_path(NRD_LIB_DIR
		libNRD.so
		PATHS ${CMAKE_CURRENT_LIST_DIR}/NRD/Lib/Release)
endif()

find_path(NRD_SHADERS_DIR
	NRD.hlsl
	PATHS ${CMAKE_CURRENT_LIST_DIR}/NRD/Shaders)

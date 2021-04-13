
file(GLOB sources "src/*.cpp" "include/*.h")

add_library(rtxdi-sdk STATIC EXCLUDE_FROM_ALL ${sources})
target_include_directories(rtxdi-sdk PUBLIC include)
set_target_properties(rtxdi-sdk PROPERTIES FOLDER "RTXDI SDK")

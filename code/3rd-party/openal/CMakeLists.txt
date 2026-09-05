cmake_minimum_required(VERSION 3.8)

project(openal32)

add_library(openal32 STATIC IMPORTED GLOBAL)
set_target_properties(openal32 PROPERTIES IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/libs/OpenAL32.lib")
target_include_directories(openal32 INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
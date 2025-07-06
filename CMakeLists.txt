cmake_minimum_required(VERSION 3.17)
project(Minecraft-mod-classifier CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build")

add_executable(Minecraft-mod-classifier src/main.cpp)

target_include_directories(Minecraft-mod-classifier PRIVATE src/include)

target_link_libraries(Minecraft-mod-classifier PRIVATE stdc++fs)

add_custom_command(
        TARGET Minecraft-mod-classifier
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_SOURCE_DIR}/assets/mods_data.json"
        "${CMAKE_SOURCE_DIR}/build/"
)
# NOTE: This CMakeLists.txt is ONLY for IDE indexing (CLion / VSCode CMake Tools).
# The real build is driven by SCons:
#
#     scons platform=windows target=template_debug arch=x86_64
#
# Do not rely on this file to produce a working DLL.

cmake_minimum_required(VERSION 3.20)
project(godot_terminal LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS "src/*.cpp" "src/*.h")

# Headers exposed by godot-cpp. The "gen/include" subtree only exists after
# godot-cpp has been built at least once via SCons.
set(GODOT_CPP_INCLUDES
    "${CMAKE_CURRENT_SOURCE_DIR}/godot-cpp/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/godot-cpp/gen/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/godot-cpp/gdextension"
)

add_library(godot_terminal_indexing STATIC EXCLUDE_FROM_ALL ${SRC_FILES})
target_include_directories(godot_terminal_indexing PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    ${GODOT_CPP_INCLUDES}
)
target_compile_definitions(godot_terminal_indexing PRIVATE
    TYPED_METHOD_BIND
    HOT_RELOAD_ENABLED
)

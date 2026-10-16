# fm4_rex - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(fm4_rex LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories("${CMAKE_CURRENT_SOURCE_DIR}/src")
include(generated/rexglue.cmake)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(FM4_LOADED_IMAGE "${CMAKE_CURRENT_SOURCE_DIR}/../fm4/loaded-image.bin" CACHE FILEPATH
    "Private image dumped from the user's executable by the build workflow")
add_custom_command(TARGET fm4_rex_codegen POST_BUILD
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/guard-generated.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/generated/default"
        "${FM4_LOADED_IMAGE}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/guard-generated.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/generated/xmedia"
        "${FM4_LOADED_IMAGE}" --module
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/guard-generated.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/generated/speech"
        "${FM4_LOADED_IMAGE}" --module
    VERBATIM)

# Sources
set(FM4REX_SOURCES
    src/main.cpp
    src/fm4-crash-diagnostics.cpp
    src/fm4-content-installer.cpp
    src/fm4-render-overrides.cpp
    src/fm4-fiber-hooks.cpp
    src/fm4-gpu-probe.cpp
    src/fm4-jpeg-diagnostics.cpp
    src/fm4-ui-diagnostics.cpp
    src/fm4-d3d-trace.cpp
    src/fm4-stream-trace.cpp
    src/fm4-streaming-fixes.cpp
    src/fm4-vblank-rate.cpp
)

if(WIN32)
    add_executable(fm4_rex WIN32 ${FM4REX_SOURCES})
else()
    add_executable(fm4_rex ${FM4REX_SOURCES})
endif()

rexglue_setup_target(fm4_rex GPU_PLUGINS xenos)
if(WIN32)
    target_link_libraries(fm4_rex PRIVATE dbghelp)
    target_link_options(fm4_rex PRIVATE "LINKER:/MAP:${CMAKE_CURRENT_BINARY_DIR}/fm4_rex.map")
endif()

add_executable(fm4_instruction_tests src/instruction-tests.cpp)
target_link_libraries(fm4_instruction_tests PRIVATE rex::runtime)
rexglue_apply_target_settings(fm4_instruction_tests)

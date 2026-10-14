cmake_minimum_required(VERSION 3.20)
project(lithium VERSION 0.0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(LITHIUM_RUNTIME_SOURCES
    runtime/src/value.cpp
    runtime/src/project.cpp
    runtime/src/thread.cpp
    runtime/src/sequencer.cpp
    runtime/src/primitives.cpp
    runtime/src/builtin_projects.cpp
    runtime/src/sb3.cpp
    runtime/src/inspect.cpp
    runtime/src/runtime.cpp
    runtime/src/c_api.cpp)

# Runtime: pure C++ with no platform dependencies, so the same sources build
# natively (tests, future Electron addon) and to WebAssembly.
add_library(lithium_runtime STATIC ${LITHIUM_RUNTIME_SOURCES})
target_include_directories(lithium_runtime PUBLIC runtime/include)
target_include_directories(lithium_runtime PRIVATE runtime/src third_party)
target_compile_options(lithium_runtime PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>)

# WebAssembly build (configure with `emcmake cmake`). Produces
# lithium_wasm.js + lithium_wasm.wasm exposing only the C API.
if(EMSCRIPTEN)
    add_executable(lithium_wasm ${LITHIUM_RUNTIME_SOURCES})
    target_include_directories(lithium_wasm PRIVATE runtime/include runtime/src third_party)
    target_link_options(lithium_wasm PRIVATE
        --no-entry
        -sMODULARIZE=1
        -sEXPORT_NAME=createLithium
        -sENVIRONMENT=web,node
        "-sEXPORTED_FUNCTIONS=_lithium_runtime_create,_lithium_runtime_destroy,_lithium_version,_lithium_load_builtin,_lithium_load_sb3_json,_lithium_get_load_log,_lithium_dump_state,_lithium_set_renderer_attached,_lithium_set_turbo_mode,_lithium_set_compatibility_mode,_lithium_use_manual_clock,_lithium_advance_clock,_lithium_green_flag,_lithium_stop_all,_lithium_runtime_step,_lithium_runtime_frame_count,_lithium_thread_count,_lithium_get_variable_number,_lithium_get_variable_string,_lithium_get_position,_malloc,_free"
        "-sEXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF8ToString,getValue")
endif()

option(LITHIUM_BUILD_TESTS "Build runtime tests" ON)
if(LITHIUM_BUILD_TESTS AND NOT EMSCRIPTEN)
    include(FetchContent)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.5.4)
    FetchContent_MakeAvailable(Catch2)
    add_executable(lithium_tests tests/runtime_test.cpp tests/value_test.cpp tests/scheduler_test.cpp tests/sb3_test.cpp)
    target_include_directories(lithium_tests PRIVATE third_party)
    target_compile_definitions(lithium_tests PRIVATE LITHIUM_TEST_DIR="${CMAKE_SOURCE_DIR}/tests")
    target_link_libraries(lithium_tests PRIVATE lithium_runtime Catch2::Catch2WithMain)
    enable_testing()
    add_test(NAME lithium_tests COMMAND lithium_tests)
endif()

cmake_minimum_required(VERSION 3.20)
project(lithium VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Runtime: pure C++ with no platform dependencies, so the same sources build
# natively (tests, future Electron addon) and to WebAssembly.
add_library(lithium_runtime STATIC
    runtime/src/runtime.cpp
    runtime/src/c_api.cpp)
target_include_directories(lithium_runtime PUBLIC runtime/include)
target_compile_options(lithium_runtime PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>)

# WebAssembly build (configure with `emcmake cmake`). Produces
# lithium_wasm.js + lithium_wasm.wasm exposing only the C API.
if(EMSCRIPTEN)
    add_executable(lithium_wasm
        runtime/src/runtime.cpp
        runtime/src/c_api.cpp)
    target_include_directories(lithium_wasm PRIVATE runtime/include)
    target_link_options(lithium_wasm PRIVATE
        --no-entry
        -sMODULARIZE=1
        -sEXPORT_NAME=createLithium
        -sENVIRONMENT=web,node
        "-sEXPORTED_FUNCTIONS=_lithium_runtime_create,_lithium_runtime_destroy,_lithium_version,_lithium_runtime_step,_lithium_runtime_frame_count"
        "-sEXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF8ToString")
endif()

option(LITHIUM_BUILD_TESTS "Build runtime tests" ON)
if(LITHIUM_BUILD_TESTS AND NOT EMSCRIPTEN)
    include(FetchContent)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.5.4)
    FetchContent_MakeAvailable(Catch2)
    add_executable(lithium_tests tests/runtime_test.cpp)
    target_link_libraries(lithium_tests PRIVATE lithium_runtime Catch2::Catch2WithMain)
    enable_testing()
    add_test(NAME lithium_tests COMMAND lithium_tests)
endif()

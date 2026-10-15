cmake_minimum_required(VERSION 3.20)
project(lithium VERSION 0.0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(LITHIUM_RUNTIME_SOURCES
    runtime/src/value.cpp
    runtime/src/text.cpp
    runtime/src/project.cpp
    runtime/src/thread.cpp
    runtime/src/sequencer.cpp
    runtime/src/primitives.cpp
    runtime/src/primitives_ext.cpp
    runtime/src/primitives_lithium.cpp
    runtime/src/packin.cpp
    runtime/src/outlist.cpp
    runtime/src/storage.cpp
    runtime/src/target_ops.cpp
    runtime/src/color.cpp
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
    # The loader reports errors via exceptions (nlohmann/json); Emscripten
    # builds without exception support by default, so a throw would abort.
    target_compile_options(lithium_wasm PRIVATE -fwasm-exceptions)
    target_link_options(lithium_wasm PRIVATE
        -fwasm-exceptions
        --no-entry
        -sMODULARIZE=1
        -sEXPORT_NAME=createLithium
        -sENVIRONMENT=web,node
        "-sEXPORTED_FUNCTIONS=_lithium_runtime_create,_lithium_runtime_destroy,_lithium_version,_lithium_load_builtin,_lithium_load_sb3_json,_lithium_get_load_log,_lithium_dump_state,_lithium_set_renderer_attached,_lithium_set_random_seed,_lithium_set_turbo_mode,_lithium_set_compatibility_mode,_lithium_use_manual_clock,_lithium_advance_clock,_lithium_green_flag,_lithium_stop_all,_lithium_runtime_step,_lithium_runtime_frame_count,_lithium_thread_count,_lithium_get_variable_number,_lithium_get_variable_string,_lithium_get_position,_lithium_set_host_callback,_lithium_asset_manifest,_lithium_set_costume_skin,_lithium_sync_renderer,_lithium_post_key,_lithium_post_mouse,_lithium_post_answer,_lithium_sound_finished,_lithium_take_redraw,_lithium_set_storage_callback,_lithium_storage_complete,_lithium_set_storage_scope,_lithium_storage_prime,_lithium_storage_area,_malloc,_free"
        "-sEXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF8ToString,getValue,setValue,addFunction,removeFunction,HEAPF64,HEAP32,HEAPU8"
        -sALLOW_TABLE_GROWTH=1)
endif()

# Native command-line runner, used by the differential tests (tests/diff).
if(NOT EMSCRIPTEN)
    add_executable(lithium_run tools/lithium_run.cpp)
    target_link_libraries(lithium_run PRIVATE lithium_runtime)
endif()

option(LITHIUM_BUILD_TESTS "Build runtime tests" ON)
if(LITHIUM_BUILD_TESTS AND NOT EMSCRIPTEN)
    include(FetchContent)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.5.4)
    FetchContent_MakeAvailable(Catch2)
    add_executable(lithium_tests tests/runtime_test.cpp tests/value_test.cpp tests/scheduler_test.cpp tests/sb3_test.cpp tests/host_test.cpp tests/lithium_ext_test.cpp)
    target_include_directories(lithium_tests PRIVATE third_party)
    target_compile_definitions(lithium_tests PRIVATE LITHIUM_TEST_DIR="${CMAKE_SOURCE_DIR}/tests")
    target_link_libraries(lithium_tests PRIVATE lithium_runtime Catch2::Catch2WithMain)
    enable_testing()
    add_test(NAME lithium_tests COMMAND lithium_tests)
endif()

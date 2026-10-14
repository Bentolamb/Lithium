#include <catch2/catch_test_macros.hpp>
#include <string>

#include "lithium/c_api.h"
#include "lithium/runtime.h"

TEST_CASE("runtime reports a version") {
    REQUIRE(lithium::Runtime::version() == "0.0.2");
}

TEST_CASE("step advances the frame counter") {
    lithium::Runtime rt;
    REQUIRE(rt.frameCount() == 0);
    rt.step();
    rt.step();
    REQUIRE(rt.frameCount() == 2);
}

TEST_CASE("C API drives a built-in project") {
    lithium_runtime* rt = lithium_runtime_create();
    REQUIRE(rt != nullptr);
    REQUIRE(std::string(lithium_version()) == "0.0.2");
    REQUIRE(lithium_load_builtin(rt, "nope") == 0);
    REQUIRE(lithium_load_builtin(rt, "repeat_counter") == 1);
    lithium_green_flag(rt);
    REQUIRE(lithium_thread_count(rt) == 1);
    lithium_runtime_step(rt);
    REQUIRE(lithium_runtime_frame_count(rt) == 1.0);
    double n = -1;
    REQUIRE(lithium_get_variable_number(rt, "Stage", "count", &n) == 1);
    REQUIRE(n == 10.0);
    REQUIRE(std::string(lithium_get_variable_string(rt, "Stage", "count")) == "10");
    REQUIRE(lithium_get_variable_string(rt, "Stage", "missing") == nullptr);
    lithium_runtime_destroy(rt);
}

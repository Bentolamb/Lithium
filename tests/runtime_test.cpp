#include <catch2/catch_test_macros.hpp>
#include <string>

#include "lithium/c_api.h"
#include "lithium/runtime.h"

TEST_CASE("runtime reports a version") {
    REQUIRE(lithium::Runtime::version() == "0.0.1");
}

TEST_CASE("step advances the frame counter") {
    lithium::Runtime rt;
    REQUIRE(rt.frameCount() == 0);
    rt.step();
    rt.step();
    REQUIRE(rt.frameCount() == 2);
}

TEST_CASE("C API matches the C++ runtime") {
    lithium_runtime* rt = lithium_runtime_create();
    REQUIRE(rt != nullptr);
    REQUIRE(std::string(lithium_version()) == "0.0.1");
    lithium_runtime_step(rt);
    REQUIRE(lithium_runtime_frame_count(rt) == 1);
    lithium_runtime_destroy(rt);
}

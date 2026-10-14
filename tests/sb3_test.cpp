#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <sstream>
#include <string>

#include "lithium/inspect.h"
#include "lithium/runtime.h"
#include "lithium/sb3.h"
#include "nlohmann/json.hpp"

using namespace lithium;

namespace {

std::string readFixture(const std::string& name) {
    std::ifstream in(std::string(LITHIUM_TEST_DIR) + "/projects/" + name + ".json", std::ios::binary);
    REQUIRE(in.good());
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

struct Fixture {
    Runtime rt;
    double now = 0, advance = 0;
    explicit Fixture(const std::string& name, double autoAdvance = 0) : advance(autoAdvance) {
        rt.setClock([this] {
            double t = now;
            now += advance;
            return t;
        });
        auto r = loadSb3Json(readFixture(name));
        REQUIRE(r.project.has_value());
        REQUIRE(r.warnings.empty());
        rt.loadProject(std::move(*r.project));
    }
    Value var(const char* target, const char* name) {
        auto v = rt.variableValue(target, name);
        REQUIRE(v.has_value());
        return *v;
    }
};

}  // namespace

TEST_CASE("sb3: rejects things that are not projects") {
    REQUIRE_FALSE(loadSb3Json("not json").project.has_value());
    REQUIRE_FALSE(loadSb3Json("{}").project.has_value());
    REQUIRE_FALSE(loadSb3Json("{\"targets\":[]}").project.has_value());
    REQUIRE(loadSb3Json("not json").error.find("valid JSON") != std::string::npos);
}

TEST_CASE("sb3 count: repeat 1000 { change count by 1 }") {
    Fixture f("count");
    f.rt.greenFlag();
    f.rt.step();
    REQUIRE(f.var("Stage", "count").toNumber() == 1000);
    REQUIRE(f.rt.threadCount() == 0);
}

TEST_CASE("sb3 mathmix: matches the values TurboWarp saved") {
    Fixture f("mathmix");
    f.rt.greenFlag();
    f.rt.step();
    // 7 + 0.1 + 0.1 + 0.1 in doubles; TurboWarp's saved file holds the same text.
    REQUIRE(f.var("Stage", "a").toString() == "7.299999999999999");
    REQUIRE(f.var("Stage", "r").toString() == "7.299999999999999");  // a + "abc" (= 0)
    REQUIRE(f.var("Stage", "s").toString() == "7.299999999999999abc");
    REQUIRE(f.var("Stage", "b").toString() == "abc");
}

TEST_CASE("sb3 slide: per-frame motion with a renderer, all at once without") {
    {
        Fixture f("slide");
        f.rt.greenFlag();
        f.rt.step();
        Target* s = f.rt.project().findTarget("Sprite1");
        REQUIRE(s->x == 110.0);
        REQUIRE(s->y == -50.0);
        REQUIRE(f.rt.threadCount() == 0);
    }
    {
        Fixture f("slide");
        f.rt.setRendererAttached(true);
        f.rt.greenFlag();
        Target* s = f.rt.project().findTarget("Sprite1");
        for (int i = 1; i <= 10; i++) {
            f.rt.step();
            REQUIRE(s->x == 10.0 * i);
            REQUIRE(s->y == -5.0 * i);
        }
        f.rt.step();  // loop exhausted, "move 10 steps" runs in the same pass
        REQUIRE(s->x == 110.0);
        REQUIRE(s->y == -50.0);
        REQUIRE(f.rt.threadCount() == 0);
    }
}

TEST_CASE("sb3 timing: waits resolve in order while another sprite spins") {
    Fixture f("timing", 1.0);
    f.rt.greenFlag();
    f.rt.step();
    const double start = f.rt.currentMSecs();
    REQUIRE(f.var("Stage", "t").toNumber() == 0);
    double firstT1 = -1, firstT2 = -1;
    for (int i = 0; i < 1000 && f.var("Stage", "t").toNumber() < 2; i++) {
        f.rt.step();
        const double t = f.var("Stage", "t").toNumber();
        if (t == 1 && firstT1 < 0) firstT1 = f.rt.currentMSecs() - start;
        if (t == 2 && firstT2 < 0) firstT2 = f.rt.currentMSecs() - start;
    }
    REQUIRE(f.var("Stage", "t").toNumber() == 2);
    REQUIRE(firstT1 >= 500.0);
    REQUIRE(firstT1 < 500.0 + 3 * f.rt.currentStepTimeMs() + 30);
    REQUIRE(firstT2 - firstT1 >= 1000.0);
    REQUIRE(f.var("Stage", "n").toNumber() > 100);  // the forever loop kept running
    REQUIRE(f.rt.threadCount() == 1);               // only the forever script is left
}

TEST_CASE("sb3: unknown opcodes load with a warning and run as no-ops") {
    const char* json = R"({"targets":[{"isStage":true,"name":"Stage","variables":{"v":["v",0]},
      "blocks":{"h":{"opcode":"event_whenflagclicked","next":"x","topLevel":true,"inputs":{},"fields":{}},
                "x":{"opcode":"looks_nonexistent_block","next":"s","inputs":{},"fields":{}},
                "s":{"opcode":"data_setvariableto","next":null,"inputs":{"VALUE":[1,[10,"ok"]]},"fields":{"VARIABLE":["v","v"]}}}}]})";
    auto r = loadSb3Json(json);
    REQUIRE(r.project.has_value());
    REQUIRE(r.warnings.size() == 1);
    REQUIRE(r.warnings[0].find("looks_nonexistent_block") != std::string::npos);
    Runtime rt;
    rt.loadProject(std::move(*r.project));
    rt.greenFlag();
    rt.step();
    REQUIRE(rt.variableValue("Stage", "v")->toString() == "ok");
}

TEST_CASE("sb3: script order follows file order, not key order") {
    // Keys "z" before "a": z must run first.
    const char* json = R"({"targets":[{"isStage":true,"name":"Stage","variables":{"v":["v",""]},
      "blocks":{
        "z":{"opcode":"event_whenflagclicked","next":"zs","topLevel":true,"inputs":{},"fields":{}},
        "zs":{"opcode":"data_setvariableto","next":null,"inputs":{"VALUE":[1,[10,"first"]]},"fields":{"VARIABLE":["v","v"]}},
        "a":{"opcode":"event_whenflagclicked","next":"as","topLevel":true,"inputs":{},"fields":{}},
        "as":{"opcode":"data_setvariableto","next":null,"inputs":{"VALUE":[1,[10,"second"]]},"fields":{"VARIABLE":["v","v"]}}}}]})";
    auto r = loadSb3Json(json);
    REQUIRE(r.project.has_value());
    Runtime rt;
    rt.loadProject(std::move(*r.project));
    rt.greenFlag();
    rt.step();
    REQUIRE(rt.variableValue("Stage", "v")->toString() == "second");  // last writer = a, which ran second
}

TEST_CASE("dumpState reports variables and positions") {
    Fixture f("slide");
    f.rt.greenFlag();
    f.rt.step();
    auto j = nlohmann::json::parse(dumpState(f.rt));
    REQUIRE(j["frames"] == 1);
    REQUIRE(j["targets"].size() == 2);
    REQUIRE(j["targets"][1]["name"] == "Sprite1");
    REQUIRE(j["targets"][1]["x"] == 110.0);
    REQUIRE(j["targets"][0]["variables"].contains("my variable"));
}

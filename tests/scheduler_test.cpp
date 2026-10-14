#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>

#include "lithium/builder.h"
#include "lithium/builtin_projects.h"
#include "lithium/runtime.h"

using namespace lithium;
using namespace lithium::build;

namespace {

// Manual clock. Every read advances it by `autoAdvance` ms so budgets end.
struct FakeClock {
    double now = 0.0;
    double autoAdvance = 0.0;
    void install(Runtime& rt) {
        rt.setClock([this] {
            double t = now;
            now += autoAdvance;
            return t;
        });
    }
};

double num(Runtime& rt, const char* target, const char* name) {
    auto v = rt.variableValue(target, name);
    REQUIRE(v.has_value());
    return v->toNumber();
}

Runtime& load(Runtime& rt, const char* name) {
    auto p = builtinProject(name);
    REQUIRE(p.has_value());
    rt.loadProject(std::move(*p));
    return rt;
}

}  // namespace

TEST_CASE("headless: a repeat loop finishes inside one step") {
    Runtime rt;
    FakeClock clock;
    clock.install(rt);
    load(rt, "repeat_counter");
    rt.greenFlag();
    REQUIRE(rt.threadCount() == 1);
    rt.step();
    REQUIRE(num(rt, "Stage", "count") == 10);
    REQUIRE(rt.threadCount() == 0);
}

TEST_CASE("renderer attached: motion requests a redraw, one loop iteration per frame") {
    Runtime rt;
    FakeClock clock;
    clock.install(rt);
    load(rt, "slide");
    rt.setRendererAttached(true);
    rt.greenFlag();
    for (int i = 1; i <= 10; i++) {
        rt.step();
        REQUIRE(rt.project().findTarget("Sprite1")->x == 10.0 * i);
        REQUIRE(rt.threadCount() == 1);
    }
    rt.step();  // repeat is exhausted
    REQUIRE(rt.threadCount() == 0);
    REQUIRE(rt.project().findTarget("Sprite1")->x == 100.0);
}

TEST_CASE("renderer attached + turbo: no redraw gate") {
    Runtime rt;
    FakeClock clock;
    clock.install(rt);
    load(rt, "slide");
    rt.setRendererAttached(true);
    rt.setTurboMode(true);
    rt.greenFlag();
    rt.step();
    REQUIRE(rt.project().findTarget("Sprite1")->x == 100.0);
    REQUIRE(rt.threadCount() == 0);
}

TEST_CASE("no renderer: motion does not end the frame early") {
    Runtime rt;
    FakeClock clock;
    clock.install(rt);
    load(rt, "slide");
    rt.greenFlag();
    rt.step();
    REQUIRE(rt.project().findTarget("Sprite1")->x == 100.0);
}

TEST_CASE("hidden sprite motion does not request a redraw") {
    Runtime rt;
    FakeClock clock;
    clock.install(rt);
    load(rt, "slide");
    rt.project().findTarget("Sprite1")->visible = false;
    rt.setRendererAttached(true);
    rt.greenFlag();
    rt.step();
    REQUIRE(rt.project().findTarget("Sprite1")->x == 100.0);
}

TEST_CASE("wait resolves on step boundaries using the step's clock") {
    Runtime rt;
    FakeClock clock;
    clock.autoAdvance = 1.0;
    clock.install(rt);
    load(rt, "wait");
    rt.greenFlag();
    rt.step();
    const double start = rt.currentMSecs();  // wait started during step 1
    REQUIRE(num(rt, "Stage", "count") == 0);
    double previous = start;
    int guard = 0;
    while (rt.threadCount() > 0 && guard++ < 1000) {
        previous = rt.currentMSecs();
        rt.step();
    }
    REQUIRE(num(rt, "Stage", "count") == 1);
    REQUIRE(rt.currentMSecs() - start >= 520.0);
    REQUIRE(previous - start < 520.0);
}

TEST_CASE("work-time budget ends a step even for a forever loop") {
    Runtime rt;
    FakeClock clock;
    clock.autoAdvance = 0.5;
    clock.install(rt);
    load(rt, "forever_counter");
    rt.greenFlag();
    rt.step();
    REQUIRE(rt.frameCount() == 1);
    REQUIRE(rt.threadCount() == 1);
    const double after1 = num(rt, "Stage", "count");
    REQUIRE(after1 > 1);
    // The step stopped at (about) 75% of the frame.
    REQUIRE(clock.now >= 0.75 * rt.currentStepTimeMs());
    rt.step();
    REQUIRE(num(rt, "Stage", "count") > after1);
}

TEST_CASE("compatibility mode shortens the frame to 1000/30 ms") {
    Runtime rt;
    REQUIRE(rt.currentStepTimeMs() == 1000.0 / 60.0);
    rt.setCompatibilityMode(true);
    REQUIRE(rt.currentStepTimeMs() == 1000.0 / 30.0);
}

TEST_CASE("stop all clears every thread") {
    Runtime rt;
    FakeClock clock;
    clock.autoAdvance = 0.5;
    clock.install(rt);
    load(rt, "forever_counter");
    rt.greenFlag();
    rt.step();
    REQUIRE(rt.threadCount() == 1);
    rt.stopAll();
    REQUIRE(rt.threadCount() == 0);
    const double c = num(rt, "Stage", "count");
    rt.step();
    REQUIRE(num(rt, "Stage", "count") == c);
}

TEST_CASE("green flag restarts running scripts") {
    Runtime rt;
    FakeClock clock;
    clock.install(rt);
    load(rt, "slide");
    rt.setRendererAttached(true);
    rt.greenFlag();
    rt.step();
    rt.step();
    REQUIRE(rt.project().findTarget("Sprite1")->x == 20.0);
    rt.greenFlag();
    rt.step();
    REQUIRE(rt.project().findTarget("Sprite1")->x == 10.0);  // went to 0 first
}

TEST_CASE("hat threads start from the top layer down") {
    Project p;
    p.stage().addVariable("log", "log", Value::string(""));
    Target& a = p.addSprite("A");
    Target& b = p.addSprite("B");
    auto append = [](Target& t, const char* tag) {
        auto& bc = t.blocks;
        BlockIndex join = bc.create("operator_join");
        bc.setInputBlock(join, "STRING1", varReporter(bc, "log"));
        bc.setInputLiteral(join, "STRING2", Value::string(tag));
        BlockIndex set = bc.create("data_setvariableto");
        bc.setField(set, "VARIABLE", Value::string("log"), "log");
        bc.setInputBlock(set, "VALUE", join);
        script(bc, {hatFlag(bc), set});
    };
    append(p.stage(), "S");
    append(a, "A");
    append(b, "B");
    Runtime rt;
    FakeClock clock;
    clock.install(rt);
    rt.loadProject(std::move(p));
    rt.greenFlag();
    rt.step();
    REQUIRE(rt.variableValue("Stage", "log")->toString() == "BAS");
}

TEST_CASE("if/else, comparison and coercing change-variable") {
    Project p;
    auto& b = p.stage().blocks;
    p.stage().addVariable("x", "x", Value::string("abc"));
    p.stage().addVariable("r", "r", Value::number(0));
    // x = "abc" + 1 -> 1 ("abc" casts to 0); if x > 0 then r = 1 else r = 2
    script(b, {hatFlag(b), changeVar(b, "x", Value::number(1)),
               ifElse(b, gt(b, varReporter(b, "x"), Value::number(0)),
                      setVar(b, "r", Value::number(1)), setVar(b, "r", Value::number(2)))});
    Runtime rt;
    FakeClock clock;
    clock.install(rt);
    rt.loadProject(std::move(p));
    rt.greenFlag();
    rt.step();
    REQUIRE(num(rt, "Stage", "x") == 1);
    REQUIRE(num(rt, "Stage", "r") == 1);
}

TEST_CASE("stop this script ends only the current script") {
    Project p;
    auto& b = p.stage().blocks;
    p.stage().addVariable("a", "a", Value::number(0));
    p.stage().addVariable("b", "b", Value::number(0));
    script(b, {hatFlag(b), stop(b, "this script"), setVar(b, "a", Value::number(1))});
    script(b, {hatFlag(b), setVar(b, "b", Value::number(1))});
    Runtime rt;
    FakeClock clock;
    clock.install(rt);
    rt.loadProject(std::move(p));
    rt.greenFlag();
    rt.step();
    REQUIRE(num(rt, "Stage", "a") == 0);
    REQUIRE(num(rt, "Stage", "b") == 1);
}

TEST_CASE("stop all from a script stops the other scripts") {
    Project p;
    auto& b = p.stage().blocks;
    p.stage().addVariable("n", "n", Value::number(0));
    p.stage().addVariable("after", "after", Value::number(0));
    script(b, {hatFlag(b), forever(b, changeVar(b, "n", Value::number(1)))});
    script(b, {hatFlag(b), stop(b, "all"), setVar(b, "after", Value::number(1))});
    Runtime rt;
    FakeClock clock;
    clock.autoAdvance = 0.5;
    clock.install(rt);
    rt.loadProject(std::move(p));
    rt.greenFlag();
    rt.step();
    REQUIRE(rt.threadCount() == 0);
    REQUIRE(num(rt, "Stage", "after") == 0);
}

TEST_CASE("unknown opcodes are skipped") {
    Project p;
    auto& b = p.stage().blocks;
    p.stage().addVariable("v", "v", Value::number(0));
    BlockIndex mystery = b.create("looks_nonexistent");
    script(b, {hatFlag(b), mystery, setVar(b, "v", Value::number(7))});
    Runtime rt;
    FakeClock clock;
    clock.install(rt);
    rt.loadProject(std::move(p));
    rt.greenFlag();
    rt.step();
    REQUIRE(num(rt, "Stage", "v") == 7);
}

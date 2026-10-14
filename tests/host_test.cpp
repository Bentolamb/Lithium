// Host-driven behaviour: input events, questions, bubbles, sound, touching, pen.
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "lithium/runtime.h"
#include "lithium/sb3.h"

using namespace lithium;

namespace {

struct SimHost : Host {
    std::vector<std::string> log;
    bool touching = false;
    int nextId = 0;
    int createDrawable(int) override { return nextId++; }
    void destroyDrawable(int, int) override {}
    void updateDrawableSkinId(int, int) override {}
    void updateDrawablePosition(int, double, double) override {}
    void updateDrawableDirectionScale(int, double, double, double) override {}
    void updateDrawableVisible(int, bool) override {}
    void updateDrawableEffect(int, const char*, double) override {}
    double setDrawableOrder(int, double, int, bool, double) override { return 0; }
    void getFencedPosition(int, double x, double y, double* ox, double* oy) override { *ox = x; *oy = y; }
    void getCurrentSkinSize(int, double* w, double* h) override { *w = 10; *h = 10; }
    bool getBounds(int, Rect* r) override { *r = Rect{-300, 300, 200, -200}; return true; }
    bool drawableTouching(int, double, double) override { return touching; }
    bool isTouchingDrawables(int, const int*, int) override { return touching; }
    bool isTouchingColor(int, const int[3]) override { return touching; }
    bool colorIsTouchingColor(int, const int[3], const int[3]) override { return false; }
    int pick(double, double) override { return -1; }
    void penClear() override { log.push_back("clear"); }
    void penStamp(int) override { log.push_back("stamp"); }
    void penPoint(const double[4], double d, double x, double y) override {
        log.push_back("point " + std::to_string((int)d) + " " + std::to_string((int)x) + " " + std::to_string((int)y));
    }
    void penLine(const double[4], double d, double x0, double y0, double x1, double y1) override {
        log.push_back("line " + std::to_string((int)d) + " " + std::to_string((int)x0) + "," + std::to_string((int)y0) + ">" +
                      std::to_string((int)x1) + "," + std::to_string((int)y1));
    }
    void setBubble(int, int, const char* type, const char* text) override { log.push_back(std::string("bubble ") + type + ":" + text); }
    void ask(const char* q) override { log.push_back(q ? std::string("ask ") + q : std::string("ask-hide")); }
    void playSound(int, int idx, int req) override { log.push_back("play " + std::to_string(idx) + " " + std::to_string(req)); }
    void stopSound(int, int) override {}
    void stopAllSounds(int) override { log.push_back("stopall"); }
    void setSoundEffects(int, double, double, double) override {}
    void targetRemoved(int) override {}
    bool has(const std::string& s) const {
        for (const auto& l : log) if (l == s) return true;
        return false;
    }
};

// A one-sprite project. `blocks` is the JSON body of the sprite's "blocks" object.
struct Sim {
    Runtime rt;
    SimHost host;
    double now = 0;
    explicit Sim(const std::string& blocks, const std::string& extras = "") {
        rt.setClock([this] { return now; });
        const std::string json = R"({"targets":[
          {"isStage":true,"name":"Stage","variables":{"v":["v",0],"a":["a",""]},"blocks":{},
           "costumes":[{"name":"b","assetId":"x","md5ext":"x.svg","dataFormat":"svg"}]},
          {"isStage":false,"name":"S","x":0,"y":0,"size":100,"direction":90,"visible":true,"variables":{},)" + extras + R"(
           "costumes":[{"name":"c1","assetId":"x","md5ext":"x.svg","dataFormat":"svg"}],
           "sounds":[{"name":"pop","assetId":"y","md5ext":"y.wav","dataFormat":"wav","rate":48000,"sampleCount":100}],
           "blocks":{)" + blocks + R"(}}]})";
        auto r = loadSb3Json(json);
        REQUIRE(r.project.has_value());
        rt.setHost(&host);
        rt.loadProject(std::move(*r.project));
    }
    double v(const char* name = "v") { return rt.variableValue("Stage", name)->toNumber(); }
    std::string s(const char* name) { return rt.variableValue("Stage", name)->toString(); }
    void frame() { now += 1000.0 / 60; rt.step(); }
};

const char* kIncV = R"("inc":{"opcode":"data_changevariableby","next":null,"inputs":{"VALUE":[1,[4,"1"]]},"fields":{"VARIABLE":["v","v"]}})";

}  // namespace

TEST_CASE("host: key hats and sensing_keypressed") {
    Sim sim(std::string(R"("h":{"opcode":"event_whenkeypressed","next":"inc","topLevel":true,"inputs":{},"fields":{"KEY_OPTION":["space",null]}},)") + kIncV);
    sim.frame();
    REQUIRE(sim.v() == 0);
    sim.rt.postKey(" ", true);
    sim.frame();
    REQUIRE(sim.v() == 1);
    REQUIRE(sim.rt.keyIsDown(Value::string("space")));
    REQUIRE(sim.rt.keyIsDown(Value::string("any")));
    sim.rt.postKey(" ", false);
    REQUIRE_FALSE(sim.rt.keyIsDown(Value::string("space")));
    sim.rt.postKey("x", true);
    sim.frame();
    REQUIRE(sim.v() == 1);
}

TEST_CASE("host: mouse position and stage click hat") {
    Sim sim(R"("x":{"opcode":"event_whenthisspriteclicked","next":null,"topLevel":true,"inputs":{},"fields":{}})");
    Runtime::MouseEvent e;
    e.hasX = e.hasY = true;
    e.x = 360; e.y = 90;  // 3/4 across, 1/4 down
    sim.rt.postMouse(e);
    REQUIRE(sim.rt.mouseX() == 120);
    REQUIRE(sim.rt.mouseY() == 90);
    e.x = 10000; e.y = 10000;
    sim.rt.postMouse(e);
    REQUIRE(sim.rt.mouseX() == 240);   // clamped
    REQUIRE(sim.rt.mouseY() == -180);
}

TEST_CASE("host: ask and wait blocks until answered") {
    Sim sim(R"("h":{"opcode":"event_whenflagclicked","next":"q","topLevel":true,"inputs":{},"fields":{}},
        "q":{"opcode":"sensing_askandwait","next":"set","inputs":{"QUESTION":[1,[10,"name?"]]},"fields":{}},
        "set":{"opcode":"data_setvariableto","next":null,"inputs":{"VALUE":[3,"ans",[10,""]]},"fields":{"VARIABLE":["a","a"]}},
        "ans":{"opcode":"sensing_answer","next":null,"parent":"set","inputs":{},"fields":{}})");
    sim.rt.greenFlag();
    sim.frame();
    REQUIRE(sim.host.has("bubble say:name?"));  // a visible sprite asks through its bubble
    REQUIRE(sim.host.has("ask "));
    sim.frame();
    REQUIRE(sim.rt.threadCount() == 1);
    sim.rt.postAnswer("bob");
    sim.frame();
    sim.frame();
    REQUIRE(sim.s("a") == "bob");
    REQUIRE(sim.rt.threadCount() == 0);
    REQUIRE(sim.host.has("bubble say:"));
}

TEST_CASE("host: say, sayforsecs and think") {
    Sim sim(R"("h":{"opcode":"event_whenflagclicked","next":"s","topLevel":true,"inputs":{},"fields":{}},
        "s":{"opcode":"looks_sayforsecs","next":"inc","inputs":{"MESSAGE":[1,[10,"hello"]],"SECS":[1,[4,"2"]]},"fields":{}},)" + std::string(kIncV));
    sim.rt.greenFlag();
    sim.frame();
    REQUIRE(sim.host.has("bubble say:hello"));
    REQUIRE(sim.v() == 0);
    sim.now += 1500;
    sim.frame();
    REQUIRE(sim.v() == 0);
    sim.now += 1000;
    sim.frame();
    sim.frame();
    REQUIRE(sim.v() == 1);
    REQUIRE(sim.host.has("bubble say:"));
}

TEST_CASE("host: play sound until done waits for the host") {
    Sim sim(R"("h":{"opcode":"event_whenflagclicked","next":"p","topLevel":true,"inputs":{},"fields":{}},
        "p":{"opcode":"sound_playuntildone","next":"inc","inputs":{"SOUND_MENU":[1,"m"]},"fields":{}},
        "m":{"opcode":"sound_sounds_menu","next":null,"parent":"p","shadow":true,"inputs":{},"fields":{"SOUND_MENU":["pop",null]}},)" + std::string(kIncV));
    sim.rt.greenFlag();
    sim.frame();
    int req = 0;
    for (const auto& l : sim.host.log) if (l.rfind("play 0 ", 0) == 0) req = std::stoi(l.substr(7));
    REQUIRE(req != 0);
    sim.frame();
    REQUIRE(sim.v() == 0);
    sim.rt.soundFinished(req);
    sim.frame();
    sim.frame();
    REQUIRE(sim.v() == 1);
}

TEST_CASE("host: touching queries go to the host; pen draws lines while moving") {
    Sim sim(R"("h":{"opcode":"event_whenflagclicked","next":"d","topLevel":true,"inputs":{},"fields":{}},
        "d":{"opcode":"pen_penDown","next":"g","inputs":{},"fields":{}},
        "g":{"opcode":"motion_gotoxy","next":"st","inputs":{"X":[1,[4,"30"]],"Y":[1,[4,"40"]]},"fields":{}},
        "st":{"opcode":"data_setvariableto","next":null,"inputs":{"VALUE":[3,"t",[10,""]]},"fields":{"VARIABLE":["v","v"]}},
        "t":{"opcode":"sensing_touchingobject","next":null,"parent":"st","inputs":{"TOUCHINGOBJECTMENU":[1,"tm"]},"fields":{}},
        "tm":{"opcode":"sensing_touchingobjectmenu","next":null,"parent":"t","shadow":true,"inputs":{},"fields":{"TOUCHINGOBJECTMENU":["_edge_",null]}})",
        R"("extensions":[],)");
    sim.host.touching = true;
    sim.rt.greenFlag();
    sim.frame();
    REQUIRE(sim.host.has("point 1 0 0"));
    REQUIRE(sim.host.has("line 1 0,0>30,40"));
    REQUIRE(sim.rt.variableValue("Stage", "v")->toString() == "true");
}

// Native runner: load a project.json (or .json produced by tests), press the
// green flag, step N frames and print the final state as JSON.
//   lithium_run <project.json> [--frames N] [--renderer] [--turbo] [--seed S] [--tick MS]
// Time is a manual clock that advances 1 ms per read, so runs are repeatable.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "lithium/inspect.h"
#include "lithium/runtime.h"
#include "lithium/sb3.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: lithium_run <project.json> [--frames N] [--renderer] [--turbo] [--seed S] [--tick MS]\n");
        return 2;
    }
    int frames = 10;
    bool renderer = false, turbo = false;
    unsigned seed = 1;
    double tick = 1.0;  // fake clock advance per read; the work budget is 12.5 reads at 1 ms
    for (int i = 2; i < argc; i++) {
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) frames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--renderer")) renderer = true;
        else if (!std::strcmp(argv[i], "--turbo")) turbo = true;
        else if (!std::strcmp(argv[i], "--tick") && i + 1 < argc) tick = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--seed") && i + 1 < argc) seed = static_cast<unsigned>(std::atoi(argv[++i]));
        else { std::fprintf(stderr, "unknown argument %s\n", argv[i]); return 2; }
    }
    std::ifstream in(argv[1], std::ios::binary);
    if (!in) { std::fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
    std::stringstream ss;
    ss << in.rdbuf();
    auto loaded = lithium::loadSb3Json(ss.str());
    for (const auto& w : loaded.warnings) std::fprintf(stderr, "warning: %s\n", w.c_str());
    if (!loaded.project) { std::fprintf(stderr, "error: %s\n", loaded.error.c_str()); return 1; }

    lithium::Runtime rt;
    double now = 0;
    rt.setClock([&now, tick] { return now += tick; });
    rt.setRandomSeed(seed);
    rt.setRendererAttached(renderer);
    rt.setTurboMode(turbo);
    rt.loadProject(std::move(*loaded.project));
    rt.greenFlag();
    for (int i = 0; i < frames; i++) rt.step();
    std::cout << lithium::dumpState(rt) << "\n";
    return 0;
}

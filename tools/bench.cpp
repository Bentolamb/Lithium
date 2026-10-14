// Wall-clock benchmark: bench <project.json> [--turbo] [--renderer]; runs until all threads finish.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include "lithium/runtime.h"
#include "lithium/sb3.h"
int main(int argc, char** argv) {
    bool turbo = false, renderer = true;
    for (int i = 2; i < argc; i++) { if (!std::strcmp(argv[i], "--turbo")) turbo = true; if (!std::strcmp(argv[i], "--norenderer")) renderer = false; }
    std::ifstream in(argv[1], std::ios::binary); std::stringstream ss; ss << in.rdbuf();
    auto loaded = lithium::loadSb3Json(ss.str());
    if (!loaded.project) { std::fprintf(stderr, "error: %s\n", loaded.error.c_str()); return 1; }
    lithium::Runtime rt;
    rt.setRendererAttached(renderer); rt.setTurboMode(turbo);
    rt.loadProject(std::move(*loaded.project));
    rt.greenFlag();
    auto t0 = std::chrono::steady_clock::now();
    int frames = 0;
    while (rt.threadCount() > 0 && frames < 100000) { rt.step(); frames++; }
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("%.1f ms, %d frames\n", ms, frames);
}

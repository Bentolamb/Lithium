// Sequencer: steps threads, ported from scratch-vm's engine/sequencer.js.
// Internal header; not part of the public API.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "lithium/project.h"
#include "lithium/thread.h"

namespace lithium {

class Runtime;

class Sequencer {
public:
    explicit Sequencer(Runtime& runtime) : runtime_(runtime) {}

    // Time a warp-mode thread may run without yielding, in ms.
    static constexpr double kWarpTimeMs = 500.0;

    // One runtime step: run threads until the work budget is spent, a redraw
    // was requested, or nothing is running. Returns the threads that finished.
    std::vector<std::shared_ptr<Thread>> stepThreads();
    // Run one thread until it yields or finishes.
    void stepThread(Thread& thread);
    void stepToBranch(Thread& thread, int branchNum, bool isLoop);
    void stepToProcedure(Thread& thread, const std::string& proccode);
    void retireThread(Thread& thread);

    // The thread currently being stepped, or nullptr.
    Thread* activeThread = nullptr;

private:
    void executeBlock(Thread& thread);
    double warpElapsed(const Thread& thread) const;

    Runtime& runtime_;
};

}  // namespace lithium

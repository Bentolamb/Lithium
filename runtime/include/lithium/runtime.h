// Lithium runtime: public C++ interface.
//
// The frontend never touches runtime internals; it goes through this class
// (or the flat C API in c_api.h, which wraps it for WASM/Node bindings).
//
// Execution model (ported from scratch-vm): the host calls step() once per
// frame (every 1000/60 ms, or 1000/30 ms in compatibility mode). A step runs
// every thread repeatedly until the work budget (75% of the frame time) is
// spent, a block requested a redraw (unless turbo mode is on), or no thread
// is still running.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "lithium/project.h"
#include "lithium/thread.h"
#include "lithium/value.h"

namespace lithium {

class Sequencer;

class Runtime {
public:
    using Clock = std::function<double()>;  // milliseconds, monotonic
    using ThreadList = std::vector<std::shared_ptr<Thread>>;

    Runtime();
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    static std::string version();

    // ---- project -------------------------------------------------------
    Project& project() { return project_; }
    // Stops everything and replaces the project.
    void loadProject(Project project);

    // ---- configuration -------------------------------------------------
    // Replaces the time source (tests and the WASM host use this).
    void setClock(Clock nowMs);
    void setTurboMode(bool on) { turboMode_ = on; }
    // 30 steps/second instead of 60, like Scratch's compatibility mode.
    void setCompatibilityMode(bool on);
    // Whether a renderer is attached. Motion blocks only request a redraw
    // (ending the frame's work early) when one is, exactly as in scratch-vm.
    void setRendererAttached(bool on) { rendererAttached_ = on; }
    // Seeds the generator behind `pick random` (Math.random in scratch-vm).
    void setRandomSeed(std::uint32_t seed) { rng_.seed(seed); }
    bool turboMode() const { return turboMode_; }
    bool rendererAttached() const { return rendererAttached_; }

    // ---- execution -----------------------------------------------------
    void greenFlag();
    void stopAll();
    void stopForTarget(Target* target, Thread* except);
    void step();

    // ---- introspection -------------------------------------------------
    std::uint64_t frameCount() const { return frames_; }
    std::size_t threadCount() const { return threads_->size(); }
    double currentStepTimeMs() const { return currentStepTimeMs_; }
    double currentMSecs() const { return currentMSecs_; }
    bool redrawRequested() const { return redrawRequested_; }
    // Looks a variable up the way a block on `target` would (local, then
    // stage). std::nullopt if the target or variable does not exist.
    std::optional<Value> variableValue(std::string_view target, std::string_view variable);

    // ---- internal: used by the sequencer and block primitives ----------
    double now() const { return clock_(); }
    void updateCurrentMSecs() { currentMSecs_ = clock_(); }
    void requestRedraw() { redrawRequested_ = true; }
    // Uniform in [0, 1), like Math.random().
    double random() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng_); }
    Sequencer& sequencer() { return *sequencer_; }
    // The live thread list. stopAll() swaps in a new list; code that is
    // iterating keeps its own shared_ptr to the old one, like scratch-vm.
    std::shared_ptr<ThreadList> threads() const { return threads_; }
    void stopThread(Thread& thread);

    // Starts every script whose hat has this opcode (and, when given, whose
    // hat field `matchField.first` equals `matchField.second` ignoring case).
    // Returns the started threads. Runtime.startHats in scratch-vm.
    using HatField = std::pair<std::string, std::string>;
    std::vector<std::shared_ptr<Thread>> startHats(std::string_view opcode, const HatField* matchField = nullptr);
    // Runtime.isActiveThread / isWaitingThread.
    static bool isActiveThread(const Thread& thread);
    static bool isWaitingThread(const Thread& thread);

private:
    std::shared_ptr<Thread> pushThread(BlockIndex topBlock, Target* target);

    Project project_;
    std::shared_ptr<ThreadList> threads_;
    std::unique_ptr<Sequencer> sequencer_;
    Clock clock_;
    double currentMSecs_ = 0.0;
    double currentStepTimeMs_ = 1000.0 / 60.0;
    bool redrawRequested_ = false;
    bool turboMode_ = false;
    bool rendererAttached_ = false;
    std::uint64_t frames_ = 0;
    std::mt19937_64 rng_{std::random_device{}()};
};

}  // namespace lithium

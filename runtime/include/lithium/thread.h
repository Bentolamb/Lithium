// Threads and stack frames, ported from scratch-vm's engine/thread.js.
//
// The behaviour that matters for compatibility is the stack discipline: the
// stack holds the block being executed at each nesting level (kNoBlock marks
// an empty branch), each level has a frame carrying loop/warp state and the
// per-block execution context (loop counters, wait timers), and a frame's
// context is reset whenever execution moves on to the next block.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "lithium/project.h"

namespace lithium {

class Thread;

// Per-block scratch state (scratch-vm: BlockUtility.stackFrame).
struct ExecutionContext {
    std::optional<double> loopCounter;  // control_repeat
    std::optional<double> timerStart;   // control_wait: runtime.currentMSecs at start
    double duration = 0.0;              // control_wait: milliseconds
    // motion_glidesecstoxy (uses the wall clock, like scratch-vm's Timer)
    std::optional<double> glideTimerStart;
    double glideDuration = 0.0;  // seconds
    double glideStartX = 0.0, glideStartY = 0.0, glideEndX = 0.0, glideEndY = 0.0;
    // procedures_call
    bool executed = false;
    std::optional<std::unordered_map<std::string, Value>> params;
    // event_broadcastandwait
    const Variable* broadcastVar = nullptr;
    bool startedThreadsSet = false;
    std::vector<std::shared_ptr<Thread>> startedThreads;

    // Same effect as `*this = ExecutionContext{}` without building a temporary.
    void clear() {
        loopCounter.reset();
        timerStart.reset();
        duration = 0.0;
        glideTimerStart.reset();
        glideDuration = 0.0;
        glideStartX = glideStartY = glideEndX = glideEndY = 0.0;
        executed = false;
        if (params) params.reset();
        broadcastVar = nullptr;
        startedThreadsSet = false;
        if (!startedThreads.empty()) startedThreads.clear();
    }
};

struct StackFrame {
    bool isLoop = false;
    bool warpMode = false;
    ExecutionContext exec;

    void reset() {
        isLoop = false;
        warpMode = false;
        exec.clear();
    }
    // Reuse the frame for the next block at the same level; warp mode is kept.
    void reuse() {
        const bool warp = warpMode;
        reset();
        warpMode = warp;
    }
};

class Thread : public std::enable_shared_from_this<Thread> {
public:
    enum class Status {
        Running = 0,
        PromiseWait = 1,  // reserved; no primitive waits on a promise yet
        Yield = 2,
        YieldTick = 3,
        Done = 4
    };

    explicit Thread(BlockIndex firstBlock) : topBlock(firstBlock) {}

    BlockIndex topBlock;
    std::vector<BlockIndex> stack;
    std::vector<StackFrame> frames;
    Status status = Status::Running;
    bool isKilled = false;
    Target* target = nullptr;
    // Start of the current warp-mode time slice (clock ms); cleared each step.
    std::optional<double> warpTimerStart;

    void pushStack(BlockIndex block);
    void reuseStackForNextBlock(BlockIndex block);
    BlockIndex popStack();
    void stopThisScript();
    // kNoBlock when the stack is empty or the top entry is an empty branch.
    BlockIndex peekStack() const;
    StackFrame* peekStackFrame();
    // Procedure parameter lookup (Thread.getParam); nullptr when not found.
    const Value* getParam(const std::string& name) const;
    bool isRecursiveCall(const std::string& proccode) const;
    void goToNextBlock();
};

}  // namespace lithium

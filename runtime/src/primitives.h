// Block primitives (the C++ side of scratch-vm's blocks/*.js).
// Internal header; not part of the public API.
#pragma once

#include <string_view>
#include <unordered_map>

#include "lithium/project.h"
#include "lithium/thread.h"
#include "lithium/value.h"

namespace lithium {

class Runtime;
class Sequencer;

// What a primitive can see and do while it runs (scratch-vm: BlockUtility plus
// the evaluated block arguments).
class PrimitiveContext {
public:
    PrimitiveContext(Runtime& runtime, Sequencer& sequencer, Thread& thread,
                     const BlockContainer& blocks, const Block& block)
        : runtime(runtime), sequencer(sequencer), thread(thread), blocks(blocks), block(block) {}

    Runtime& runtime;
    Sequencer& sequencer;
    Thread& thread;
    const BlockContainer& blocks;
    const Block& block;

    Target& target() const { return *thread.target; }

    // Value of an input: a literal, or the result of the nested reporter.
    // A missing input reads as "" (stands in for JavaScript's undefined).
    Value input(std::string_view name) const;
    // input(name).toNumber() without re-parsing literal text on every execution.
    double inputNumber(std::string_view name) const;
    const Field* field(std::string_view name) const;

    // Per-block state that survives re-execution of the same block.
    ExecutionContext& stackFrame() const { return thread.peekStackFrame()->exec; }
    bool stackTimerNeedsInit() const;
    void startStackTimer(double durationMs) const;
    bool stackTimerFinished() const;

    void yield() const { thread.status = Thread::Status::Yield; }
    void yieldTick() const { thread.status = Thread::Status::YieldTick; }
    void startBranch(int branchNum, bool isLoop) const;
    void startProcedure(const std::string& proccode) const;
};

using CommandFn = void (*)(PrimitiveContext&);
using ReporterFn = Value (*)(PrimitiveContext&);

struct Primitive {
    enum class Kind { Hat, Command, Reporter };

    const char* opcode;
    Kind kind;
    CommandFn command;    // Command; also an optional predicate for hats
    ReporterFn reporter;  // Reporter
    bool restartExistingThreads;  // hats only
    bool edgeActivated = false;   // hats only: `reporter` is the predicate
};

using PrimitiveMap = std::unordered_map<std::string_view, const Primitive*>;
// Looks, sensing, sound, pen, extra motion and events (primitives_ext.cpp).
void registerExtPrimitives(PrimitiveMap& map);

// nullptr for opcodes without an implementation.
const Primitive* lookupPrimitive(std::string_view opcode);
// Resolves and caches the primitive for a block.
const Primitive* primitiveFor(const Block& block);

}  // namespace lithium

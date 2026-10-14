#include "sequencer.h"

#include <cstddef>
#include <limits>

#include "lithium/runtime.h"
#include "primitives.h"

namespace lithium {

std::vector<std::shared_ptr<Thread>> Sequencer::stepThreads() {
    // Work time is 75% of the thread stepping interval.
    const double workTime = 0.75 * runtime_.currentStepTimeMs();
    // For Scratch 2 compatibility the millisecond clock is read once per step.
    runtime_.updateCurrentMSecs();
    const double timerStart = runtime_.now();
    stepStart_ = timerStart;
    stepBudget_ = workTime;
    loopPasses_ = 0;
    budgetLeft_ = true;

    std::size_t numActiveThreads = std::numeric_limits<std::size_t>::max();  // "Infinity"
    bool ranFirstTick = false;
    // Reading the clock is costly (a JS call under WebAssembly), so the budget is
    // re-checked every few passes instead of on every pass of a tight loop.
    unsigned passes = 0;
    bool timeLeft = true;
    std::vector<std::shared_ptr<Thread>> doneThreads;

    // Keep stepping while: there are active threads, the budget is not spent,
    // and (turbo mode or no primitive requested a redraw).
    while (!runtime_.threads()->empty() && numActiveThreads > 0 &&
           timeLeft &&
           (runtime_.turboMode() || !runtime_.redrawRequested())) {
        if ((++passes & 7u) == 0) timeLeft = (runtime_.now() - timerStart) < workTime;
        numActiveThreads = 0;
        bool stoppedThread = false;
        // Hold the list we started with: stopAll() swaps the runtime's list
        // for a new one while this pass keeps going over the old one.
        const std::shared_ptr<Runtime::ThreadList> threads = runtime_.threads();
        for (std::size_t i = 0; i < threads->size(); i++) {
            const std::shared_ptr<Thread> keepAlive = (*threads)[i];
            Thread& active = *keepAlive;
            activeThread = &active;
            // Skip finished threads.
            if (active.stack.empty() || active.status == Thread::Status::Done) {
                stoppedThread = true;
                continue;
            }
            if (active.status == Thread::Status::YieldTick && !ranFirstTick) {
                // Clear the single-tick yield from the previous step.
                active.status = Thread::Status::Running;
            }
            if (active.status == Thread::Status::Running || active.status == Thread::Status::Yield) {
                stepThread(active);
                active.warpTimerStart.reset();
                if (active.isKilled) {
                    i--;  // wraps for i == 0; the loop's i++ brings it back
                }
            }
            if (active.status == Thread::Status::Running) numActiveThreads++;
            if (active.stack.empty() || active.status == Thread::Status::Done) {
                stoppedThread = true;
            }
        }
        // We ticked once; YieldTick threads must not run again this step.
        ranFirstTick = true;

        // Drop inactive threads from the runtime's current list.
        if (stoppedThread) {
            const std::shared_ptr<Runtime::ThreadList> current = runtime_.threads();
            std::size_t next = 0;
            for (std::size_t i = 0; i < current->size(); i++) {
                const std::shared_ptr<Thread> thread = (*current)[i];
                if (!thread->stack.empty() && thread->status != Thread::Status::Done) {
                    (*current)[next++] = thread;
                } else {
                    doneThreads.push_back(thread);
                }
            }
            current->resize(next);
        }
    }

    activeThread = nullptr;
    return doneThreads;
}

bool Sequencer::canRepeatLoopNow(const Thread& thread) {
    if (thread.status != Thread::Status::Running || thread.isKilled) return false;
    const auto& threads = *runtime_.threads();
    if (threads.size() != 1 || threads[0].get() != &thread) return false;
    if (!runtime_.turboMode() && runtime_.redrawRequested()) return false;
    if ((++loopPasses_ & 7u) == 0) budgetLeft_ = (runtime_.now() - stepStart_) < stepBudget_;
    return budgetLeft_;
}

double Sequencer::warpElapsed(const Thread& thread) const {
    return thread.warpTimerStart ? runtime_.now() - *thread.warpTimerStart : 0.0;
}

void Sequencer::stepThread(Thread& thread) {
    BlockIndex currentBlockId = thread.peekStack();
    if (currentBlockId == kNoBlock) {
        // A "null block": an empty branch.
        thread.popStack();
        // Did the null follow a hat block?
        if (thread.stack.empty()) {
            thread.status = Thread::Status::Done;
            return;
        }
    }
    while ((currentBlockId = thread.peekStack()) != kNoBlock) {
        bool isWarpMode = thread.peekStackFrame()->warpMode;
        if (isWarpMode && !thread.warpTimerStart) {
            // Start counting this thread toward kWarpTimeMs.
            thread.warpTimerStart = runtime_.now();
        }
        if (thread.target == nullptr) {
            retireThread(thread);
        } else {
            executeBlock(thread);
        }
        // If the thread has yielded or is waiting, yield to other threads.
        if (thread.status == Thread::Status::Yield) {
            // Mark as running for the next iteration.
            thread.status = Thread::Status::Running;
            // In warp mode, yielded blocks are re-executed immediately.
            if (isWarpMode && warpElapsed(thread) <= kWarpTimeMs) continue;
            return;
        } else if (thread.status == Thread::Status::PromiseWait) {
            return;
        } else if (thread.status == Thread::Status::YieldTick) {
            // stepThreads resets the thread to Running on the next step.
            return;
        }
        // If no control flow has happened, switch to the next block.
        if (thread.peekStack() == currentBlockId) {
            thread.goToNextBlock();
        }
        // If no next block was found, look further up the stack.
        while (thread.peekStack() == kNoBlock) {
            thread.popStack();
            if (thread.stack.empty()) {
                // No more stack to run.
                thread.status = Thread::Status::Done;
                return;
            }
            StackFrame* frame = thread.peekStackFrame();
            isWarpMode = frame->warpMode;
            if (frame->isLoop) {
                // The current level is a loop: yield for the frame, unless in
                // warp mode where only a spent warp timer yields. Do not move
                // to the next block, since loops are re-executed.
                if (!isWarpMode || warpElapsed(thread) > kWarpTimeMs) {
                    if (canRepeatLoopNow(thread)) continue;
                    return;
                }
                continue;
            }
            // Get the next block of the existing block on the stack.
            thread.goToNextBlock();
        }
    }
}

void Sequencer::stepToBranch(Thread& thread, int branchNum, bool isLoop) {
    if (branchNum == 0) branchNum = 1;
    const BlockIndex currentBlockId = thread.peekStack();
    const BlockIndex branchId = thread.target->blocks.getBranch(currentBlockId, branchNum);
    thread.peekStackFrame()->isLoop = isLoop;
    // Push the branch (kNoBlock for an empty branch).
    thread.pushStack(branchId);
}

void Sequencer::stepToProcedure(Thread& thread, const std::string& proccode) {
    const BlockContainer& blocks = thread.target->blocks;
    const BlockIndex definition = blocks.procedureDefinition(proccode);
    if (definition == kNoBlock) return;
    // Check if the call is recursive, before the definition is pushed.
    const bool isRecursive = thread.isRecursiveCall(proccode);
    thread.pushStack(definition);
    // In known warp-mode threads, only yield when time is up.
    if (thread.peekStackFrame()->warpMode && warpElapsed(thread) > kWarpTimeMs) {
        thread.status = Thread::Status::Yield;
        return;
    }
    // Look for the warp flag on the definition's prototype.
    bool doWarp = false;
    const Input* custom = blocks.at(definition).findInput("custom_block");
    if (custom != nullptr && custom->kind == Input::Kind::Block && blocks.valid(custom->block)) {
        const Block& inner = blocks.at(custom->block);
        if (inner.mutation) doWarp = inner.mutation->warp;
    }
    if (doWarp) {
        thread.peekStackFrame()->warpMode = true;
    } else if (isRecursive) {
        // In normal-mode threads, yield any time we have a recursive call.
        thread.status = Thread::Status::Yield;
    }
}

void Sequencer::retireThread(Thread& thread) {
    thread.stack.clear();
    thread.frames.clear();
    thread.status = Thread::Status::Done;
}

void Sequencer::executeBlock(Thread& thread) {
    Target& target = *thread.target;
    const BlockIndex id = thread.peekStack();
    if (!target.blocks.valid(id)) {
        // Block no longer exists: stop the thread.
        retireThread(thread);
        return;
    }
    const Block& block = target.blocks.at(id);
    const Primitive* primitive = primitiveFor(block);
    // Unknown opcodes are skipped as no-ops, like scratch-vm.
    if (primitive == nullptr) return;
    PrimitiveContext cx(runtime_, *this, thread, target.blocks, block);
    switch (primitive->kind) {
        case Primitive::Kind::Hat:
            if (primitive->command != nullptr) primitive->command(cx);
            break;
        case Primitive::Kind::Command:
            primitive->command(cx);
            break;
        case Primitive::Kind::Reporter:
            (void)primitive->reporter(cx);
            break;
    }
}

}  // namespace lithium

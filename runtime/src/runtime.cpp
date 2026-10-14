#include "lithium/runtime.h"

#include <algorithm>
#include <chrono>

#include "primitives.h"
#include "sequencer.h"

namespace lithium {

namespace {

double steadyClockMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

}  // namespace

Runtime::Runtime()
    : threads_(std::make_shared<ThreadList>()),
      sequencer_(std::make_unique<Sequencer>(*this)),
      clock_(&steadyClockMs) {
    currentMSecs_ = clock_();
}

Runtime::~Runtime() = default;

std::string Runtime::version() { return "0.0.2"; }

void Runtime::loadProject(Project project) {
    stopAll();
    project_ = std::move(project);
    redrawRequested_ = false;
}

void Runtime::setClock(Clock nowMs) {
    clock_ = std::move(nowMs);
    currentMSecs_ = clock_();
}

void Runtime::setCompatibilityMode(bool on) {
    currentStepTimeMs_ = on ? 1000.0 / 30.0 : 1000.0 / 60.0;
}

void Runtime::greenFlag() {
    stopAll();
    startHats("event_whenflagclicked");
}

void Runtime::stopAll() {
    // If a thread is mid-step, retire it. The list is replaced rather than
    // cleared: a step in progress keeps going over the old list, as in
    // scratch-vm.
    if (sequencer_->activeThread != nullptr) stopThread(*sequencer_->activeThread);
    threads_ = std::make_shared<ThreadList>();
}

void Runtime::stopForTarget(Target* target, Thread* except) {
    for (const std::shared_ptr<Thread>& thread : *threads_) {
        if (thread.get() == except) continue;
        if (thread->target == target) stopThread(*thread);
    }
}

void Runtime::stopThread(Thread& thread) {
    // Mark for later removal and tell the sequencer to stop running it.
    thread.isKilled = true;
    sequencer_->retireThread(thread);
}

void Runtime::step() {
    // Clean up threads that were told to stop during or since the last step.
    threads_->erase(std::remove_if(threads_->begin(), threads_->end(),
                                   [](const std::shared_ptr<Thread>& t) { return t->isKilled; }),
                    threads_->end());
    // (Edge-activated hats are evaluated here once they exist.)
    redrawRequested_ = false;
    sequencer_->stepThreads();
    // The renderer draws here in the host; it reads target state after step().
    ++frames_;
}

std::shared_ptr<Thread> Runtime::pushThread(BlockIndex topBlock, Target* target) {
    auto thread = std::make_shared<Thread>(topBlock);
    thread->target = target;
    thread->pushStack(topBlock);
    threads_->push_back(thread);
    return thread;
}

void Runtime::startHats(std::string_view opcode) {
    const Primitive* hat = lookupPrimitive(opcode);
    if (hat == nullptr || hat->kind != Primitive::Kind::Hat) return;
    const auto& targets = project_.targets();
    // Top layer first, like scratch-vm's allScriptsByOpcodeDo.
    for (std::size_t t = targets.size(); t-- > 0;) {
        Target* target = targets[t].get();
        for (const BlockIndex script : target->blocks.scripts()) {
            if (target->blocks.at(script).opcode != opcode) continue;
            if (hat->restartExistingThreads) {
                // Restart in place so the thread keeps its position in the list.
                bool restarted = false;
                for (std::shared_ptr<Thread>& existing : *threads_) {
                    if (existing->target == target && existing->topBlock == script) {
                        auto fresh = std::make_shared<Thread>(script);
                        fresh->target = target;
                        fresh->pushStack(script);
                        existing = fresh;
                        restarted = true;
                        break;
                    }
                }
                if (restarted) continue;
            } else {
                // Give up if a thread with this top block is already running.
                bool running = false;
                for (const std::shared_ptr<Thread>& existing : *threads_) {
                    if (existing->target == target && existing->topBlock == script &&
                        existing->status != Thread::Status::Done) {
                        running = true;
                        break;
                    }
                }
                if (running) continue;
            }
            pushThread(script, target);
        }
    }
}

std::optional<Value> Runtime::variableValue(std::string_view target, std::string_view variable) {
    Target* t = project_.findTarget(target);
    if (t == nullptr) return std::nullopt;
    Variable* v = t->lookupVariableByName(variable);
    if (v == nullptr) return std::nullopt;
    return v->value;
}

}  // namespace lithium

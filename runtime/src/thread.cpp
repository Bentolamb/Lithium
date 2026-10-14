#include "lithium/thread.h"

namespace lithium {

void Thread::pushStack(BlockIndex block) {
    stack.push_back(block);
    // Push a frame if we need one; not needed if we just popped the stack.
    if (stack.size() > frames.size()) {
        StackFrame frame;
        frame.warpMode = !frames.empty() && frames.back().warpMode;
        frames.push_back(frame);
    }
}

void Thread::reuseStackForNextBlock(BlockIndex block) {
    if (stack.empty() || frames.empty()) return;
    stack.back() = block;
    frames.back().reuse();
}

BlockIndex Thread::popStack() {
    if (!frames.empty()) frames.pop_back();
    if (stack.empty()) return kNoBlock;
    const BlockIndex top = stack.back();
    stack.pop_back();
    return top;
}

void Thread::stopThisScript() {
    // Stops at a procedures_call block, so a stop inside a custom block
    // only leaves that block.
    BlockIndex block = peekStack();
    while (block != kNoBlock) {
        if (target->blocks.valid(block) && target->blocks.at(block).opcode == "procedures_call") break;
        popStack();
        block = peekStack();
    }
    if (stack.empty()) status = Status::Done;
}

BlockIndex Thread::peekStack() const {
    return stack.empty() ? kNoBlock : stack.back();
}

StackFrame* Thread::peekStackFrame() {
    return frames.empty() ? nullptr : &frames.back();
}

const Value* Thread::getParam(const std::string& name) const {
    for (std::size_t i = frames.size(); i-- > 0;) {
        const auto& params = frames[i].exec.params;
        if (!params) continue;
        const auto it = params->find(name);
        return it == params->end() ? nullptr : &it->second;
    }
    return nullptr;
}

bool Thread::isRecursiveCall(const std::string& proccode) const {
    int callCount = 5;  // max number of enclosing procedure calls to examine
    const std::ptrdiff_t sp = static_cast<std::ptrdiff_t>(stack.size()) - 1;
    for (std::ptrdiff_t i = sp - 1; i >= 0; i--) {
        const BlockIndex b = stack[static_cast<std::size_t>(i)];
        if (target->blocks.valid(b)) {
            const Block& block = target->blocks.at(b);
            if (block.opcode == "procedures_call" && block.mutation && block.mutation->proccode == proccode) return true;
        }
        if (--callCount < 0) return false;
    }
    return false;
}

void Thread::goToNextBlock() {
    const BlockIndex next = target->blocks.getNextBlock(peekStack());
    reuseStackForNextBlock(next);
}

}  // namespace lithium

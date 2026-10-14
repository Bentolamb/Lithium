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
    // Procedures are not implemented yet; once they are, this must stop at a
    // procedures_call block like scratch-vm does.
    BlockIndex block = peekStack();
    while (block != kNoBlock) {
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

void Thread::goToNextBlock() {
    const BlockIndex next = target->blocks.getNextBlock(peekStack());
    reuseStackForNextBlock(next);
}

}  // namespace lithium

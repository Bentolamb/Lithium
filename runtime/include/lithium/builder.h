// Small helpers for assembling block scripts in C++ (tests, built-in demo
// projects). The SB3 loader will build the same structures from JSON.
#pragma once

#include <initializer_list>
#include <string>

#include "lithium/project.h"

namespace lithium::build {

using B = BlockContainer;

inline BlockIndex hatFlag(B& b) {
    BlockIndex h = b.create("event_whenflagclicked");
    b.setTopLevel(h);
    return h;
}

// Links blocks into a stack (a.next = b, ...) and returns the first one.
inline BlockIndex chain(B& b, std::initializer_list<BlockIndex> blocks) {
    BlockIndex first = kNoBlock, prev = kNoBlock;
    for (BlockIndex x : blocks) {
        if (first == kNoBlock) first = x;
        if (prev != kNoBlock) b.setNext(prev, x);
        prev = x;
    }
    return first;
}

// Makes `first` a top-level script (call after chain()).
inline BlockIndex script(B& b, std::initializer_list<BlockIndex> blocks) {
    BlockIndex first = chain(b, blocks);
    b.setTopLevel(first);
    return first;
}

inline BlockIndex setVar(B& b, const std::string& name, Value v) {
    BlockIndex x = b.create("data_setvariableto");
    b.setField(x, "VARIABLE", Value::string(name), name);
    b.setInputLiteral(x, "VALUE", std::move(v));
    return x;
}

inline BlockIndex changeVar(B& b, const std::string& name, Value v) {
    BlockIndex x = b.create("data_changevariableby");
    b.setField(x, "VARIABLE", Value::string(name), name);
    b.setInputLiteral(x, "VALUE", std::move(v));
    return x;
}

inline BlockIndex varReporter(B& b, const std::string& name) {
    BlockIndex x = b.create("data_variable");
    b.setField(x, "VARIABLE", Value::string(name), name);
    return x;
}

inline BlockIndex repeat(B& b, Value times, BlockIndex body) {
    BlockIndex x = b.create("control_repeat");
    b.setInputLiteral(x, "TIMES", std::move(times));
    b.setBranch(x, "SUBSTACK", body);
    return x;
}

inline BlockIndex forever(B& b, BlockIndex body) {
    BlockIndex x = b.create("control_forever");
    b.setBranch(x, "SUBSTACK", body);
    return x;
}

inline BlockIndex wait(B& b, Value seconds) {
    BlockIndex x = b.create("control_wait");
    b.setInputLiteral(x, "DURATION", std::move(seconds));
    return x;
}

inline BlockIndex stop(B& b, const std::string& option) {
    BlockIndex x = b.create("control_stop");
    b.setField(x, "STOP_OPTION", Value::string(option));
    return x;
}

inline BlockIndex ifThen(B& b, BlockIndex cond, BlockIndex body) {
    BlockIndex x = b.create("control_if");
    b.setInputBlock(x, "CONDITION", cond);
    b.setBranch(x, "SUBSTACK", body);
    return x;
}

inline BlockIndex ifElse(B& b, BlockIndex cond, BlockIndex a, BlockIndex c) {
    BlockIndex x = b.create("control_if_else");
    b.setInputBlock(x, "CONDITION", cond);
    b.setBranch(x, "SUBSTACK", a);
    b.setBranch(x, "SUBSTACK2", c);
    return x;
}

inline BlockIndex changeX(B& b, Value dx) {
    BlockIndex x = b.create("motion_changexby");
    b.setInputLiteral(x, "DX", std::move(dx));
    return x;
}

inline BlockIndex gotoXY(B& b, Value x, Value y) {
    BlockIndex r = b.create("motion_gotoxy");
    b.setInputLiteral(r, "X", std::move(x));
    b.setInputLiteral(r, "Y", std::move(y));
    return r;
}

// Binary operator with two literal-or-block operands. Pass kNoBlock for a
// block to use the literal instead.
inline BlockIndex binary(B& b, const char* opcode, const char* in1, const char* in2,
                         Value l1, BlockIndex b1, Value l2, BlockIndex b2) {
    BlockIndex x = b.create(opcode);
    if (b1 != kNoBlock) b.setInputBlock(x, in1, b1); else b.setInputLiteral(x, in1, std::move(l1));
    if (b2 != kNoBlock) b.setInputBlock(x, in2, b2); else b.setInputLiteral(x, in2, std::move(l2));
    return x;
}

inline BlockIndex add(B& b, BlockIndex l, Value r) {
    return binary(b, "operator_add", "NUM1", "NUM2", Value::number(0), l, std::move(r), kNoBlock);
}
inline BlockIndex lt(B& b, BlockIndex l, Value r) {
    return binary(b, "operator_lt", "OPERAND1", "OPERAND2", Value::number(0), l, std::move(r), kNoBlock);
}
inline BlockIndex gt(B& b, BlockIndex l, Value r) {
    return binary(b, "operator_gt", "OPERAND1", "OPERAND2", Value::number(0), l, std::move(r), kNoBlock);
}
inline BlockIndex equals(B& b, BlockIndex l, Value r) {
    return binary(b, "operator_equals", "OPERAND1", "OPERAND2", Value::number(0), l, std::move(r), kNoBlock);
}

}  // namespace lithium::build

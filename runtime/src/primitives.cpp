#include "primitives.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "lithium/runtime.h"
#include "sequencer.h"

namespace lithium {

namespace {

// ---------------------------------------------------------------- evaluation

Value evaluateReporter(const PrimitiveContext& parent, BlockIndex index) {
    if (!parent.blocks.valid(index)) return Value::string("");
    const Block& block = parent.blocks.at(index);
    const Primitive* primitive = primitiveFor(block);
    // Unknown reporters read as undefined in scratch-vm; "" is the closest
    // value that behaves the same under number and boolean casts.
    if (primitive == nullptr || primitive->kind != Primitive::Kind::Reporter) {
        return Value::string("");
    }
    PrimitiveContext cx(parent.runtime, parent.sequencer, parent.thread, parent.blocks, block);
    return primitive->reporter(cx);
}

// -------------------------------------------------------------------- events
// event_whenflagclicked is a hat without a predicate: executing it is a no-op.

// ------------------------------------------------------------------- control

void controlRepeat(PrimitiveContext& cx) {
    const double times = jsRound(cx.input("TIMES").toNumber());
    ExecutionContext& ec = cx.stackFrame();
    // Initialize the loop.
    if (!ec.loopCounter) ec.loopCounter = times;
    // Only execute once per frame. When the branch finishes this block runs
    // again, and the exhausted counter falls through to the next block.
    *ec.loopCounter -= 1.0;
    if (*ec.loopCounter >= 0.0) cx.startBranch(1, true);
}

void controlRepeatUntil(PrimitiveContext& cx) {
    if (!cx.input("CONDITION").toBoolean()) cx.startBranch(1, true);
}

void controlWhile(PrimitiveContext& cx) {
    if (cx.input("CONDITION").toBoolean()) cx.startBranch(1, true);
}

void controlForever(PrimitiveContext& cx) { cx.startBranch(1, true); }

void controlWait(PrimitiveContext& cx) {
    if (cx.stackTimerNeedsInit()) {
        const double duration = std::max(0.0, 1000.0 * cx.input("DURATION").toNumber());
        cx.startStackTimer(duration);
        cx.runtime.requestRedraw();
        cx.yield();
    } else if (!cx.stackTimerFinished()) {
        cx.yield();
    }
}

void controlWaitUntil(PrimitiveContext& cx) {
    if (!cx.input("CONDITION").toBoolean()) cx.yield();
}

void controlIf(PrimitiveContext& cx) {
    if (cx.input("CONDITION").toBoolean()) cx.startBranch(1, false);
}

void controlIfElse(PrimitiveContext& cx) {
    if (cx.input("CONDITION").toBoolean()) {
        cx.startBranch(1, false);
    } else {
        cx.startBranch(2, false);
    }
}

void controlStop(PrimitiveContext& cx) {
    const Field* option = cx.field("STOP_OPTION");
    if (option == nullptr) return;
    const std::string value = option->value.toString();
    if (value == "all") {
        cx.runtime.stopAll();
    } else if (value == "other scripts in sprite" || value == "other scripts in stage") {
        cx.runtime.stopForTarget(&cx.target(), &cx.thread);
    } else if (value == "this script") {
        cx.thread.stopThisScript();
    }
}

// ---------------------------------------------------------------------- data

void dataSetVariableTo(PrimitiveContext& cx) {
    const Field* field = cx.field("VARIABLE");
    if (field == nullptr) return;
    Variable& variable = cx.target().lookupOrCreateVariable(field->id, field->value.toString());
    variable.value = cx.input("VALUE");
}

void dataChangeVariableBy(PrimitiveContext& cx) {
    const Field* field = cx.field("VARIABLE");
    if (field == nullptr) return;
    Variable& variable = cx.target().lookupOrCreateVariable(field->id, field->value.toString());
    const double casted = variable.value.toNumber();
    const double delta = cx.input("VALUE").toNumber();
    variable.value = Value::number(casted + delta);
}

Value dataVariable(PrimitiveContext& cx) {
    const Field* field = cx.field("VARIABLE");
    if (field == nullptr) return Value::string("");
    return cx.target().lookupOrCreateVariable(field->id, field->value.toString()).value;
}

// ----------------------------------------------------------------- operators

Value operatorAdd(PrimitiveContext& cx) {
    return Value::number(cx.input("NUM1").toNumber() + cx.input("NUM2").toNumber());
}
Value operatorSubtract(PrimitiveContext& cx) {
    return Value::number(cx.input("NUM1").toNumber() - cx.input("NUM2").toNumber());
}
Value operatorMultiply(PrimitiveContext& cx) {
    return Value::number(cx.input("NUM1").toNumber() * cx.input("NUM2").toNumber());
}
Value operatorDivide(PrimitiveContext& cx) {
    return Value::number(cx.input("NUM1").toNumber() / cx.input("NUM2").toNumber());
}
Value operatorLt(PrimitiveContext& cx) {
    return Value::boolean(Value::compare(cx.input("OPERAND1"), cx.input("OPERAND2")) < 0);
}
Value operatorEquals(PrimitiveContext& cx) {
    return Value::boolean(Value::compare(cx.input("OPERAND1"), cx.input("OPERAND2")) == 0);
}
Value operatorGt(PrimitiveContext& cx) {
    return Value::boolean(Value::compare(cx.input("OPERAND1"), cx.input("OPERAND2")) > 0);
}
Value operatorAnd(PrimitiveContext& cx) {
    // Both operands are evaluated, like scratch-vm (no short-circuit of inputs).
    const bool a = cx.input("OPERAND1").toBoolean();
    const bool b = cx.input("OPERAND2").toBoolean();
    return Value::boolean(a && b);
}
Value operatorOr(PrimitiveContext& cx) {
    const bool a = cx.input("OPERAND1").toBoolean();
    const bool b = cx.input("OPERAND2").toBoolean();
    return Value::boolean(a || b);
}
Value operatorNot(PrimitiveContext& cx) {
    return Value::boolean(!cx.input("OPERAND").toBoolean());
}
Value operatorJoin(PrimitiveContext& cx) {
    return Value::string(cx.input("STRING1").toString() + cx.input("STRING2").toString());
}

// -------------------------------------------------------------------- motion

constexpr double kPi = 3.141592653589793238462643383279502884;

double degToRad(double deg) { return deg * kPi / 180.0; }

// RenderedTarget.setXY. With no renderer attached the position is stored as is
// and no redraw is requested. Fencing the sprite to the stage needs costume
// bounds from the renderer and is not implemented yet.
void setTargetXY(PrimitiveContext& cx, double x, double y) {
    Target& target = cx.target();
    if (target.isStage) return;
    target.x = x;
    target.y = y;
    if (cx.runtime.rendererAttached() && target.visible) cx.runtime.requestRedraw();
}

// Scratch 2's snapToInteger.
double limitPrecision(double coordinate) {
    const double rounded = jsRound(coordinate);
    const double delta = coordinate - rounded;
    return std::fabs(delta) < 1e-9 ? rounded : coordinate;
}

void motionMoveSteps(PrimitiveContext& cx) {
    const double steps = cx.input("STEPS").toNumber();
    const double radians = degToRad(90.0 - cx.target().direction);
    const double dx = steps * std::cos(radians);
    const double dy = steps * std::sin(radians);
    setTargetXY(cx, cx.target().x + dx, cx.target().y + dy);
}
void motionGoToXY(PrimitiveContext& cx) {
    setTargetXY(cx, cx.input("X").toNumber(), cx.input("Y").toNumber());
}
void motionChangeXBy(PrimitiveContext& cx) {
    setTargetXY(cx, cx.target().x + cx.input("DX").toNumber(), cx.target().y);
}
void motionSetX(PrimitiveContext& cx) {
    setTargetXY(cx, cx.input("X").toNumber(), cx.target().y);
}
void motionChangeYBy(PrimitiveContext& cx) {
    setTargetXY(cx, cx.target().x, cx.target().y + cx.input("DY").toNumber());
}
void motionSetY(PrimitiveContext& cx) {
    setTargetXY(cx, cx.target().x, cx.input("Y").toNumber());
}
Value motionXPosition(PrimitiveContext& cx) {
    return Value::number(limitPrecision(cx.target().x));
}
Value motionYPosition(PrimitiveContext& cx) {
    return Value::number(limitPrecision(cx.target().y));
}
Value motionDirection(PrimitiveContext& cx) { return Value::number(cx.target().direction); }

// --------------------------------------------------------------------- table

using Kind = Primitive::Kind;

const Primitive kPrimitives[] = {
    {"event_whenflagclicked", Kind::Hat, nullptr, nullptr, true},

    {"control_repeat", Kind::Command, &controlRepeat, nullptr, false},
    {"control_repeat_until", Kind::Command, &controlRepeatUntil, nullptr, false},
    {"control_while", Kind::Command, &controlWhile, nullptr, false},
    {"control_forever", Kind::Command, &controlForever, nullptr, false},
    {"control_wait", Kind::Command, &controlWait, nullptr, false},
    {"control_wait_until", Kind::Command, &controlWaitUntil, nullptr, false},
    {"control_if", Kind::Command, &controlIf, nullptr, false},
    {"control_if_else", Kind::Command, &controlIfElse, nullptr, false},
    {"control_stop", Kind::Command, &controlStop, nullptr, false},

    {"data_setvariableto", Kind::Command, &dataSetVariableTo, nullptr, false},
    {"data_changevariableby", Kind::Command, &dataChangeVariableBy, nullptr, false},
    {"data_variable", Kind::Reporter, nullptr, &dataVariable, false},

    {"operator_add", Kind::Reporter, nullptr, &operatorAdd, false},
    {"operator_subtract", Kind::Reporter, nullptr, &operatorSubtract, false},
    {"operator_multiply", Kind::Reporter, nullptr, &operatorMultiply, false},
    {"operator_divide", Kind::Reporter, nullptr, &operatorDivide, false},
    {"operator_lt", Kind::Reporter, nullptr, &operatorLt, false},
    {"operator_equals", Kind::Reporter, nullptr, &operatorEquals, false},
    {"operator_gt", Kind::Reporter, nullptr, &operatorGt, false},
    {"operator_and", Kind::Reporter, nullptr, &operatorAnd, false},
    {"operator_or", Kind::Reporter, nullptr, &operatorOr, false},
    {"operator_not", Kind::Reporter, nullptr, &operatorNot, false},
    {"operator_join", Kind::Reporter, nullptr, &operatorJoin, false},

    {"motion_movesteps", Kind::Command, &motionMoveSteps, nullptr, false},
    {"motion_gotoxy", Kind::Command, &motionGoToXY, nullptr, false},
    {"motion_changexby", Kind::Command, &motionChangeXBy, nullptr, false},
    {"motion_setx", Kind::Command, &motionSetX, nullptr, false},
    {"motion_changeyby", Kind::Command, &motionChangeYBy, nullptr, false},
    {"motion_sety", Kind::Command, &motionSetY, nullptr, false},
    {"motion_xposition", Kind::Reporter, nullptr, &motionXPosition, false},
    {"motion_yposition", Kind::Reporter, nullptr, &motionYPosition, false},
    {"motion_direction", Kind::Reporter, nullptr, &motionDirection, false},
};

}  // namespace

// ------------------------------------------------------------ PrimitiveContext

Value PrimitiveContext::input(std::string_view name) const {
    const Input* in = block.findInput(name);
    if (in == nullptr) return Value::string("");
    switch (in->kind) {
        case Input::Kind::Literal:
            return in->literal;
        case Input::Kind::Block:
            return evaluateReporter(*this, in->block);
        case Input::Kind::None:
            break;
    }
    return Value::string("");
}

const Field* PrimitiveContext::field(std::string_view name) const {
    return block.findField(name);
}

bool PrimitiveContext::stackTimerNeedsInit() const { return !stackFrame().timerStart; }

void PrimitiveContext::startStackTimer(double durationMs) const {
    ExecutionContext& ec = stackFrame();
    // Like scratch-vm, the timer reads runtime.currentMSecs, which is updated
    // once per step; waits therefore resolve on step boundaries.
    ec.timerStart = runtime.currentMSecs();
    ec.duration = durationMs;
}

bool PrimitiveContext::stackTimerFinished() const {
    const ExecutionContext& ec = stackFrame();
    const double elapsed = runtime.currentMSecs() - *ec.timerStart;
    return !(elapsed < ec.duration);
}

void PrimitiveContext::startBranch(int branchNum, bool isLoop) const {
    sequencer.stepToBranch(thread, branchNum, isLoop);
}

// -------------------------------------------------------------------- lookup

const Primitive* lookupPrimitive(std::string_view opcode) {
    static const std::unordered_map<std::string_view, const Primitive*> table = [] {
        std::unordered_map<std::string_view, const Primitive*> m;
        for (const Primitive& p : kPrimitives) m.emplace(p.opcode, &p);
        return m;
    }();
    const auto it = table.find(opcode);
    return it == table.end() ? nullptr : it->second;
}

const Primitive* primitiveFor(const Block& block) {
    if (!block.primitiveResolved) {
        block.primitive = lookupPrimitive(block.opcode);
        block.primitiveResolved = true;
    }
    return block.primitive;
}

}  // namespace lithium

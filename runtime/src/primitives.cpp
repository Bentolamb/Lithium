#include "primitives.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <unordered_map>

#include "lithium/runtime.h"
#include "lithium/text.h"
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

// ---------------------------------------------------------------- procedures

void procedureCall(PrimitiveContext& cx) {
    ExecutionContext& ec = cx.stackFrame();
    if (ec.executed) return;
    if (!cx.block.mutation) return;
    const std::string& proccode = cx.block.mutation->proccode;
    const Mutation* proto = cx.blocks.procedurePrototype(proccode);
    // Could not find the procedure: no-op, like Scratch 2.
    if (proto == nullptr) return;
    // Evaluate the arguments first: they must see the caller's parameters.
    std::unordered_map<std::string, Value> params;
    for (std::size_t i = 0; i < proto->argumentIds.size(); i++) {
        const std::string& id = proto->argumentIds[i];
        const std::string name = i < proto->argumentNames.size() ? proto->argumentNames[i] : std::string();
        const Input* in = cx.block.findInput(id);
        Value v = (in != nullptr && in->kind != Input::Kind::None)
                      ? cx.input(id)
                      : (i < proto->argumentDefaults.size() ? proto->argumentDefaults[i] : Value::string(""));
        params[name] = std::move(v);
    }
    // Initialize params for this frame even when there are none, so that
    // getParam does not look at earlier frames.
    if (!ec.params) ec.params.emplace();
    for (auto& kv : params) (*ec.params)[kv.first] = std::move(kv.second);
    ec.executed = true;
    cx.startProcedure(proccode);
}

Value argumentReporter(PrimitiveContext& cx) {
    const Field* f = cx.field("VALUE");
    if (f == nullptr) return Value::number(0.0);
    const Value* v = cx.thread.getParam(f->value.toString());
    // When the parameter is not found in the most recent procedure call,
    // the default is always 0.
    return v == nullptr ? Value::number(0.0) : *v;
}

// -------------------------------------------------------------------- clones

void controlCreateCloneOf(PrimitiveContext& cx) {
    const std::string option = cx.input("CLONE_OPTION").toString();
    Target* source = nullptr;
    if (option == "_myself_") {
        source = &cx.target();
    } else {
        for (const auto& t : cx.runtime.project().targets()) {
            if (!t->isStage && t->isOriginal && t->name == option) {
                source = t.get();
                break;
            }
        }
    }
    if (source == nullptr) return;
    cx.runtime.createClone(*source);
}

void controlDeleteThisClone(PrimitiveContext& cx) {
    if (cx.target().isOriginal) return;
    cx.runtime.disposeClone(cx.target());
}

Value controlCloneMenuEmpty(PrimitiveContext&) { return Value::string(""); }

Value controlCloneMenu(PrimitiveContext& cx) {
    const Field* f = cx.field("CLONE_OPTION");
    return f == nullptr ? Value::string("") : f->value;
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


// -------------------------------------------------------------------- events

// The (id, name) pair scratch-vm builds for a broadcast block.
struct BroadcastRef {
    std::string id;    // empty = null
    std::string name;
};

BroadcastRef broadcastOption(PrimitiveContext& cx) {
    const Input* in = cx.block.findInput("BROADCAST_INPUT");
    if (in != nullptr && in->kind == Input::Kind::Literal) return {in->id, in->literal.toString()};
    // A reporter is plugged in: cast to string, no id.
    return {std::string(), cx.input("BROADCAST_INPUT").toString()};
}

void eventBroadcast(PrimitiveContext& cx) {
    const BroadcastRef ref = broadcastOption(cx);
    const Variable* var = cx.runtime.project().stage().lookupBroadcastMsg(ref.id, ref.name);
    if (var != nullptr) {
        const Runtime::HatField match{"BROADCAST_OPTION", var->name};
        cx.runtime.startHats("event_whenbroadcastreceived", &match);
    }
}

void eventBroadcastAndWait(PrimitiveContext& cx) {
    ExecutionContext& ec = cx.stackFrame();
    if (ec.broadcastVar == nullptr) {
        const BroadcastRef ref = broadcastOption(cx);
        ec.broadcastVar = cx.runtime.project().stage().lookupBroadcastMsg(ref.id, ref.name);
    }
    if (ec.broadcastVar == nullptr) return;
    // Have we run before, starting threads?
    if (!ec.startedThreadsSet) {
        const Runtime::HatField match{"BROADCAST_OPTION", ec.broadcastVar->name};
        ec.startedThreads = cx.runtime.startHats("event_whenbroadcastreceived", &match);
        ec.startedThreadsSet = true;
        if (ec.startedThreads.empty()) return;  // nothing was started
    }
    // Scratch 2 considers threads to be waiting while they are still in the
    // runtime's list, even if they have run all their blocks.
    const auto live = cx.runtime.threads();
    bool waiting = false;
    for (const auto& started : ec.startedThreads) {
        if (std::find(live->begin(), live->end(), started) != live->end()) { waiting = true; break; }
    }
    if (!waiting) return;
    // If every started thread waits for the next tick or later, yield for a
    // tick as well. Otherwise yield until the next loop of the threads.
    bool allWaiting = true;
    for (const auto& started : ec.startedThreads) {
        if (!Runtime::isWaitingThread(*started)) { allWaiting = false; break; }
    }
    if (allWaiting) cx.yieldTick();
    else cx.yield();
}

// ---------------------------------------------------------------------- lists

constexpr std::size_t kListItemLimit = 200000;
constexpr long kListInvalid = -1;
constexpr long kListAll = -2;

// Cast.toListIndex: a 1-based index, or kListInvalid / kListAll.
long toListIndex(PrimitiveContext& cx, const Value& index, std::size_t length, bool acceptAll) {
    if (index.isString()) {
        const std::string& s = index.toString();
        if (s == "all") return acceptAll ? kListAll : kListInvalid;
        if (s == "last") return length > 0 ? static_cast<long>(length) : kListInvalid;
        if (s == "random" || s == "any") {
            return length > 0 ? 1 + static_cast<long>(std::floor(cx.runtime.random() * static_cast<double>(length))) : kListInvalid;
        }
    }
    const double d = std::floor(index.toNumber());
    if (d < 1 || d > static_cast<double>(length)) return kListInvalid;
    return static_cast<long>(d);
}

Variable& listOf(PrimitiveContext& cx) {
    const Field* f = cx.field("LIST");
    static Variable scratch;  // never reached for well-formed blocks
    if (f == nullptr) { scratch = Variable{}; scratch.type = Variable::Type::List; return scratch; }
    return cx.target().lookupOrCreateList(f->id, f->value.toString());
}

Value dataListContents(PrimitiveContext& cx) {
    const Variable& list = listOf(cx);
    // If the list is all single letters, join with no separator; else with a space.
    bool allSingleLetters = true;
    for (const Value& item : list.list) {
        if (!(item.isString() && utf8ToUtf16(item.toString()).size() == 1)) { allSingleLetters = false; break; }
    }
    std::string out;
    for (std::size_t i = 0; i < list.list.size(); i++) {
        if (i > 0 && !allSingleLetters) out += ' ';
        out += list.list[i].toString();
    }
    // Re-join surrogate halves that ended up adjacent.
    return Value::string(utf16ToUtf8(utf8ToUtf16(out)));
}

void dataAddToList(PrimitiveContext& cx) {
    Variable& list = listOf(cx);
    if (list.list.size() < kListItemLimit) list.list.push_back(cx.input("ITEM"));
}

void dataDeleteOfList(PrimitiveContext& cx) {
    Variable& list = listOf(cx);
    const long index = toListIndex(cx, cx.input("INDEX"), list.list.size(), true);
    if (index == kListInvalid) return;
    if (index == kListAll) { list.list.clear(); return; }
    list.list.erase(list.list.begin() + (index - 1));
}

void dataDeleteAllOfList(PrimitiveContext& cx) { listOf(cx).list.clear(); }

void dataInsertAtList(PrimitiveContext& cx) {
    const Value item = cx.input("ITEM");
    Variable& list = listOf(cx);
    const long index = toListIndex(cx, cx.input("INDEX"), list.list.size() + 1, false);
    if (index == kListInvalid) return;
    if (static_cast<std::size_t>(index) > kListItemLimit) return;
    list.list.insert(list.list.begin() + (index - 1), item);
    // If inserting caused the list to grow past the limit, drop the last item.
    if (list.list.size() > kListItemLimit) list.list.pop_back();
}

void dataReplaceItemOfList(PrimitiveContext& cx) {
    const Value item = cx.input("ITEM");
    Variable& list = listOf(cx);
    const long index = toListIndex(cx, cx.input("INDEX"), list.list.size(), false);
    if (index == kListInvalid) return;
    list.list[static_cast<std::size_t>(index - 1)] = item;
}

Value dataItemOfList(PrimitiveContext& cx) {
    Variable& list = listOf(cx);
    const long index = toListIndex(cx, cx.input("INDEX"), list.list.size(), false);
    if (index == kListInvalid) return Value::string("");
    return list.list[static_cast<std::size_t>(index - 1)];
}

Value dataItemNumOfList(PrimitiveContext& cx) {
    const Value item = cx.input("ITEM");
    const Variable& list = listOf(cx);
    // Cast.compare, so that 123 and "123" are the same item.
    for (std::size_t i = 0; i < list.list.size(); i++) {
        if (Value::compare(list.list[i], item) == 0) return Value::number(static_cast<double>(i + 1));
    }
    return Value::number(0);
}

Value dataLengthOfList(PrimitiveContext& cx) {
    return Value::number(static_cast<double>(listOf(cx).list.size()));
}

Value dataListContainsItem(PrimitiveContext& cx) {
    const Value item = cx.input("ITEM");
    const Variable& list = listOf(cx);
    for (const Value& v : list.list) {
        if (Value::compare(v, item) == 0) return Value::boolean(true);
    }
    return Value::boolean(false);
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
    return Value::string(concatJs(cx.input("STRING1").toString(), cx.input("STRING2").toString()));
}


std::string asciiLowerCopy(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

// Cast.isInt
bool castIsInt(const Value& v) {
    if (v.isBoolean()) return true;
    if (v.isNumber()) {
        const double d = v.toJsNumber();
        if (std::isnan(d)) return true;
        return std::isfinite(d) && std::fabs(d) < 1e21 && d == std::trunc(d);
    }
    return v.toString().find('.') == std::string::npos;
}

// parseFloat(x.toFixed(10)): JavaScript rounds to 10 decimals before parsing.
double roundTo10(double x) {
    if (!std::isfinite(x)) return x;
    char buf[400];
    std::snprintf(buf, sizeof buf, "%.10f", x);
    return std::strtod(buf, nullptr);
}

Value operatorMod(PrimitiveContext& cx) {
    const double n = cx.input("NUM1").toNumber();
    const double modulus = cx.input("NUM2").toNumber();
    double result = std::fmod(n, modulus);  // same as JavaScript's %
    // Scratch mod uses floored division instead of truncated division.
    if (result / modulus < 0) result += modulus;
    return Value::number(result);
}
Value operatorRound(PrimitiveContext& cx) {
    return Value::number(jsRound(cx.input("NUM").toNumber()));
}
Value operatorRandom(PrimitiveContext& cx) {
    const Value from = cx.input("FROM");
    const Value to = cx.input("TO");
    const double nFrom = from.toNumber();
    const double nTo = to.toNumber();
    const double low = nFrom <= nTo ? nFrom : nTo;
    const double high = nFrom <= nTo ? nTo : nFrom;
    if (low == high) return Value::number(low);
    // If both arguments are ints, truncate the result to an int.
    if (castIsInt(from) && castIsInt(to)) {
        return Value::number(low + std::floor(cx.runtime.random() * ((high + 1) - low)));
    }
    return Value::number(cx.runtime.random() * (high - low) + low);
}
Value operatorLetterOf(PrimitiveContext& cx) {
    const double index = cx.input("LETTER").toNumber() - 1;
    const std::u16string str = utf8ToUtf16(cx.input("STRING").toString());
    if (index < 0 || index >= static_cast<double>(str.size())) return Value::string("");
    return Value::string(utf16ToUtf8(std::u16string(1, str[static_cast<std::size_t>(index)])));
}
Value operatorLength(PrimitiveContext& cx) {
    return Value::number(static_cast<double>(utf8ToUtf16(cx.input("STRING").toString()).size()));
}
Value operatorContains(PrimitiveContext& cx) {
    const std::string a = jsToLowerCase(cx.input("STRING1").toString());
    const std::string b = jsToLowerCase(cx.input("STRING2").toString());
    return Value::boolean(a.find(b) != std::string::npos);
}
Value operatorMathop(PrimitiveContext& cx) {
    const Field* f = cx.field("OPERATOR");
    const std::string op = asciiLowerCopy(f ? f->value.toString() : std::string());
    const double n = cx.input("NUM").toNumber();
    constexpr double kPiLocal = 3.141592653589793238462643383279502884;
    if (op == "abs") return Value::number(std::fabs(n));
    if (op == "floor") return Value::number(std::floor(n));
    if (op == "ceiling") return Value::number(std::ceil(n));
    if (op == "sqrt") return Value::number(std::sqrt(n));
    if (op == "sin") return Value::number(roundTo10(std::sin((kPiLocal * n) / 180)));
    if (op == "cos") return Value::number(roundTo10(std::cos((kPiLocal * n) / 180)));
    if (op == "tan") {
        const double angle = std::fmod(n, 360.0);
        if (angle == -270 || angle == 90) return Value::number(std::numeric_limits<double>::infinity());
        if (angle == -90 || angle == 270) return Value::number(-std::numeric_limits<double>::infinity());
        return Value::number(roundTo10(std::tan((kPiLocal * angle) / 180)));
    }
    if (op == "asin") return Value::number((std::asin(n) * 180) / kPiLocal);
    if (op == "acos") return Value::number((std::acos(n) * 180) / kPiLocal);
    if (op == "atan") return Value::number((std::atan(n) * 180) / kPiLocal);
    if (op == "ln") return Value::number(std::log(n));
    if (op == "log") return Value::number(std::log(n) / 2.302585092994046);  // Math.LN10
    if (op == "e ^") return Value::number(std::exp(n));
    if (op == "10 ^") return Value::number(std::pow(10.0, n));
    return Value::number(0);
}

// -------------------------------------------------------------------- motion

constexpr double kPi = 3.141592653589793238462643383279502884;

double degToRad(double deg) { return deg * kPi / 180.0; }

void setTargetXY(PrimitiveContext& cx, double x, double y) { cx.runtime.setXY(cx.target(), x, y); }

// Scratch 2's snapToInteger.
double limitPrecision(double coordinate) {
    const double rounded = jsRound(coordinate);
    const double delta = coordinate - rounded;
    return std::fabs(delta) < 1e-9 ? rounded : coordinate;
}


void setTargetDirection(PrimitiveContext& cx, double direction) { cx.runtime.setDirection(cx.target(), direction); }
void motionTurnRight(PrimitiveContext& cx) {
    const double degrees = cx.input("DEGREES").toNumber();
    setTargetDirection(cx, cx.target().direction + degrees);
}
void motionTurnLeft(PrimitiveContext& cx) {
    const double degrees = cx.input("DEGREES").toNumber();
    setTargetDirection(cx, cx.target().direction - degrees);
}
void motionPointInDirection(PrimitiveContext& cx) {
    setTargetDirection(cx, cx.input("DIRECTION").toNumber());
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

    {"event_whenbroadcastreceived", Kind::Hat, nullptr, nullptr, true},
    {"event_broadcast", Kind::Command, &eventBroadcast, nullptr, false},
    {"event_broadcastandwait", Kind::Command, &eventBroadcastAndWait, nullptr, false},

    {"data_listcontents", Kind::Reporter, nullptr, &dataListContents, false},
    {"data_addtolist", Kind::Command, &dataAddToList, nullptr, false},
    {"data_deleteoflist", Kind::Command, &dataDeleteOfList, nullptr, false},
    {"data_deletealloflist", Kind::Command, &dataDeleteAllOfList, nullptr, false},
    {"data_insertatlist", Kind::Command, &dataInsertAtList, nullptr, false},
    {"data_replaceitemoflist", Kind::Command, &dataReplaceItemOfList, nullptr, false},
    {"data_itemoflist", Kind::Reporter, nullptr, &dataItemOfList, false},
    {"data_itemnumoflist", Kind::Reporter, nullptr, &dataItemNumOfList, false},
    {"data_lengthoflist", Kind::Reporter, nullptr, &dataLengthOfList, false},
    {"data_listcontainsitem", Kind::Reporter, nullptr, &dataListContainsItem, false},

    {"control_repeat", Kind::Command, &controlRepeat, nullptr, false},
    {"control_repeat_until", Kind::Command, &controlRepeatUntil, nullptr, false},
    {"control_while", Kind::Command, &controlWhile, nullptr, false},
    {"control_forever", Kind::Command, &controlForever, nullptr, false},
    {"control_wait", Kind::Command, &controlWait, nullptr, false},
    {"control_wait_until", Kind::Command, &controlWaitUntil, nullptr, false},
    {"control_if", Kind::Command, &controlIf, nullptr, false},
    {"control_if_else", Kind::Command, &controlIfElse, nullptr, false},
    {"control_stop", Kind::Command, &controlStop, nullptr, false},
    {"control_create_clone_of", Kind::Command, &controlCreateCloneOf, nullptr, false},
    {"control_create_clone_of_menu", Kind::Reporter, nullptr, &controlCloneMenu, false},
    {"control_delete_this_clone", Kind::Command, &controlDeleteThisClone, nullptr, false},
    {"control_start_as_clone", Kind::Hat, nullptr, nullptr, false},

    {"procedures_definition", Kind::Hat, nullptr, nullptr, false},
    {"procedures_prototype", Kind::Reporter, nullptr, &controlCloneMenuEmpty, false},
    {"procedures_call", Kind::Command, &procedureCall, nullptr, false},
    {"argument_reporter_string_number", Kind::Reporter, nullptr, &argumentReporter, false},
    {"argument_reporter_boolean", Kind::Reporter, nullptr, &argumentReporter, false},

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

    {"operator_random", Kind::Reporter, nullptr, &operatorRandom, false},
    {"operator_letter_of", Kind::Reporter, nullptr, &operatorLetterOf, false},
    {"operator_length", Kind::Reporter, nullptr, &operatorLength, false},
    {"operator_contains", Kind::Reporter, nullptr, &operatorContains, false},
    {"operator_mod", Kind::Reporter, nullptr, &operatorMod, false},
    {"operator_round", Kind::Reporter, nullptr, &operatorRound, false},
    {"operator_mathop", Kind::Reporter, nullptr, &operatorMathop, false},

    {"motion_turnright", Kind::Command, &motionTurnRight, nullptr, false},
    {"motion_turnleft", Kind::Command, &motionTurnLeft, nullptr, false},
    {"motion_pointindirection", Kind::Command, &motionPointInDirection, nullptr, false},
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

void PrimitiveContext::startProcedure(const std::string& proccode) const {
    sequencer.stepToProcedure(thread, proccode);
}

void PrimitiveContext::startBranch(int branchNum, bool isLoop) const {
    sequencer.stepToBranch(thread, branchNum, isLoop);
}

// -------------------------------------------------------------------- lookup

const Primitive* lookupPrimitive(std::string_view opcode) {
    static const std::unordered_map<std::string_view, const Primitive*> table = [] {
        std::unordered_map<std::string_view, const Primitive*> m;
        for (const Primitive& p : kPrimitives) m.emplace(p.opcode, &p);
        registerExtPrimitives(m);
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

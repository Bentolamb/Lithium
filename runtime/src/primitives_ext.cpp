// Looks, motion, sensing, events, sound, pen and a few control blocks,
// ported from scratch-vm's blocks/scratch3_*.js and the pen extension.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <limits>

#include "lithium/color.h"
#include "lithium/runtime.h"
#include "lithium/text.h"
#include "primitives.h"
#include "sequencer.h"

namespace lithium {

namespace {

using Kind = Primitive::Kind;

double clampd(double n, double min, double max) { return std::min(std::max(n, min), max); }
constexpr double kPi = 3.141592653589793238462643383279502884;
double degToRad(double d) { return d * kPi / 180.0; }
double radToDeg(double r) { return r * 180.0 / kPi; }

// A menu shadow block (looks_costume, motion_goto_menu, ...): its single field's value.
Value menuReporter(PrimitiveContext& cx) {
    if (cx.block.fields.empty()) return Value::string("");
    return cx.block.fields.front().second.value;
}

std::string fieldString(PrimitiveContext& cx, const char* name) {
    const Field* f = cx.field(name);
    return f == nullptr ? std::string() : f->value.toString();
}

// JavaScript parseInt(String(v), 10): NaN when no digits.
double jsParseInt(const std::string& s) {
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
    bool neg = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) neg = s[i++] == '-';
    double v = 0;
    bool any = false;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        v = v * 10 + (s[i++] - '0');
        any = true;
    }
    if (!any) return std::numeric_limits<double>::quiet_NaN();
    return neg ? -v : v;
}

// ---------------------------------------------------------------------- looks

void renderBubble(PrimitiveContext& cx) {
    Target& t = cx.target();
    if (cx.runtime.host() == nullptr) return;
    cx.runtime.host()->setBubble(t.uid, t.drawableId, t.bubble.type.c_str(),
                                 (t.visible && !t.bubble.text.empty()) ? t.bubble.text.c_str() : "");
}

void lookSay(PrimitiveContext& cx) { cx.runtime.setBubble(cx.target(), "say", cx.input("MESSAGE")); }
void lookThink(PrimitiveContext& cx) { cx.runtime.setBubble(cx.target(), "think", cx.input("MESSAGE")); }

void bubbleForSecs(PrimitiveContext& cx, const char* type) {
    cx.runtime.setBubble(cx.target(), type, cx.input("MESSAGE"));
    Target* target = &cx.target();
    const std::uint64_t usageId = target->bubble.usageId;
    const int uid = target->uid;
    Runtime* rt = &cx.runtime;
    double ms = 1000.0 * cx.input("SECS").toJsNumber();
    if (!(ms > 0)) ms = 0;  // setTimeout clamps NaN and negatives to 0
    const std::string t = type;
    rt->waitForPromise(cx.thread, Runtime::PromiseKind::Timer, rt->now() + ms, 0, [rt, uid, usageId, t] {
        Target* tg = nullptr;
        for (const auto& c : rt->project().targets()) if (c->uid == uid) tg = c.get();
        if (tg != nullptr && tg->bubble.usageId == usageId) rt->setBubble(*tg, t, Value::string(""));
    });
}
void lookSayForSecs(PrimitiveContext& cx) { bubbleForSecs(cx, "say"); }
void lookThinkForSecs(PrimitiveContext& cx) { bubbleForSecs(cx, "think"); }

void lookShow(PrimitiveContext& cx) {
    cx.runtime.setVisible(cx.target(), true);
    renderBubble(cx);
}
void lookHide(PrimitiveContext& cx) {
    cx.runtime.setVisible(cx.target(), false);
    renderBubble(cx);
}
void noopCommand(PrimitiveContext&) {}

void setCostumeFromArg(Runtime& rt, Target& t, const Value& requested, bool zeroIndex) {
    if (requested.isNumber()) {
        rt.setCostume(t, zeroIndex ? requested.toNumber() : requested.toNumber() - 1);
        return;
    }
    const int index = t.costumeIndexByName(requested.toString());
    if (index != -1) {
        rt.setCostume(t, index);
    } else if (requested.isString() && requested.toString() == "next costume") {
        rt.setCostume(t, t.currentCostume + 1);
    } else if (requested.isString() && requested.toString() == "previous costume") {
        rt.setCostume(t, t.currentCostume - 1);
    } else if (!(std::isnan(requested.toJsNumber()) || Value::isWhiteSpace(requested))) {
        const double n = requested.toJsNumber();
        rt.setCostume(t, zeroIndex ? n : n - 1);
    }
}

std::vector<std::shared_ptr<Thread>> setBackdrop(Runtime& rt, const Value& requested, bool zeroIndex) {
    Target& stage = *rt.stageTarget();
    if (requested.isNumber()) {
        rt.setCostume(stage, zeroIndex ? requested.toNumber() : requested.toNumber() - 1);
    } else {
        const std::string name = requested.toString();
        const int index = stage.costumeIndexByName(name);
        if (index != -1) {
            rt.setCostume(stage, index);
        } else if (requested.isString() && name == "next backdrop") {
            rt.setCostume(stage, stage.currentCostume + 1);
        } else if (requested.isString() && name == "previous backdrop") {
            rt.setCostume(stage, stage.currentCostume - 1);
        } else if (requested.isString() && name == "random backdrop") {
            const int count = static_cast<int>(stage.costumes().size());
            if (count > 1) {
                const double lower = 0, upper = count - 1, excluded = stage.currentCostume;
                const double options = upper - lower;
                double r = lower + std::floor(rt.random() * options);
                if (r >= excluded) r += 1;
                rt.setCostume(stage, r);
            }
        } else if (!(std::isnan(requested.toJsNumber()) || Value::isWhiteSpace(requested))) {
            const double n = requested.toJsNumber();
            rt.setCostume(stage, zeroIndex ? n : n - 1);
        }
    }
    std::string newName;
    if (!stage.costumes().empty()) newName = stage.costumes()[static_cast<std::size_t>(stage.currentCostume)].name;
    const Runtime::HatField match{"BACKDROP", newName};
    return rt.startHats("event_whenbackdropswitchesto", &match);
}

void lookSwitchCostume(PrimitiveContext& cx) { setCostumeFromArg(cx.runtime, cx.target(), cx.input("COSTUME"), false); }
void lookNextCostume(PrimitiveContext& cx) {
    setCostumeFromArg(cx.runtime, cx.target(), Value::number(cx.target().currentCostume + 1), true);
}
void lookSwitchBackdrop(PrimitiveContext& cx) { setBackdrop(cx.runtime, cx.input("BACKDROP"), false); }
void lookNextBackdrop(PrimitiveContext& cx) {
    setBackdrop(cx.runtime, Value::number(cx.runtime.stageTarget()->currentCostume + 1), true);
}

void lookSwitchBackdropAndWait(PrimitiveContext& cx) {
    ExecutionContext& ec = cx.stackFrame();
    if (!ec.startedThreadsSet) {
        ec.startedThreads = setBackdrop(cx.runtime, cx.input("BACKDROP"), false);
        ec.startedThreadsSet = true;
        if (ec.startedThreads.empty()) return;
    }
    const auto threads = cx.runtime.threads();
    bool waiting = false;
    for (const auto& t : ec.startedThreads) {
        if (std::find(threads->begin(), threads->end(), t) != threads->end()) waiting = true;
    }
    if (waiting) {
        bool all = true;
        for (const auto& t : ec.startedThreads) all = all && Runtime::isWaitingThread(*t);
        if (all) cx.yieldTick(); else cx.yield();
    }
}

double clampEffect(const std::string& effect, double value) {
    if (effect == "ghost") return clampd(value, 0, 100);
    if (effect == "brightness") return clampd(value, -100, 100);
    return value;
}

void lookChangeEffect(PrimitiveContext& cx) {
    const std::string effect = jsToLowerCase(fieldString(cx, "EFFECT"));
    const double change = cx.input("CHANGE").toNumber();
    double* slot = cx.target().effects.byName(effect);
    if (slot == nullptr) return;
    cx.runtime.setEffect(cx.target(), effect, clampEffect(effect, change + *slot));
}
void lookSetEffect(PrimitiveContext& cx) {
    const std::string effect = jsToLowerCase(fieldString(cx, "EFFECT"));
    const double value = clampEffect(effect, cx.input("VALUE").toNumber());
    cx.runtime.setEffect(cx.target(), effect, value);
}
void lookClearEffects(PrimitiveContext& cx) { cx.runtime.clearEffects(cx.target()); }
void lookChangeSize(PrimitiveContext& cx) {
    cx.runtime.setSize(cx.target(), cx.target().size + cx.input("CHANGE").toNumber());
}
void lookSetSize(PrimitiveContext& cx) { cx.runtime.setSize(cx.target(), cx.input("SIZE").toNumber()); }
void lookGoToFrontBack(PrimitiveContext& cx) {
    if (cx.target().isStage) return;
    if (fieldString(cx, "FRONT_BACK") == "front") cx.runtime.goToFront(cx.target());
    else cx.runtime.goToBack(cx.target());
}
void lookGoForwardBackward(PrimitiveContext& cx) {
    if (cx.target().isStage) return;
    const double n = cx.input("NUM").toNumber();
    if (fieldString(cx, "FORWARD_BACKWARD") == "forward") cx.runtime.goForwardLayers(cx.target(), n);
    else cx.runtime.goBackwardLayers(cx.target(), n);
}
Value lookSize(PrimitiveContext& cx) { return Value::number(jsRound(cx.target().size)); }

Value costumeNumberName(Target& t, const std::string& which) {
    if (which == "number") return Value::number(t.currentCostume + 1);
    const auto& costumes = t.costumes();
    if (t.currentCostume < 0 || static_cast<std::size_t>(t.currentCostume) >= costumes.size()) return Value::string("");
    return Value::string(costumes[static_cast<std::size_t>(t.currentCostume)].name);
}
Value lookCostumeNumberName(PrimitiveContext& cx) { return costumeNumberName(cx.target(), fieldString(cx, "NUMBER_NAME")); }
Value lookBackdropNumberName(PrimitiveContext& cx) {
    return costumeNumberName(*cx.runtime.stageTarget(), fieldString(cx, "NUMBER_NAME"));
}

// --------------------------------------------------------------------- motion

// Motion.getTargetXY; false when the target does not exist.
bool targetXY(PrimitiveContext& cx, const Value& what, double* x, double* y) {
    const std::string name = what.toString();
    if (what.isString() && name == "_mouse_") {
        *x = cx.runtime.mouseX();
        *y = cx.runtime.mouseY();
    } else if (what.isString() && name == "_random_") {
        *x = jsRound(480.0 * (cx.runtime.random() - 0.5));
        *y = jsRound(360.0 * (cx.runtime.random() - 0.5));
    } else {
        Target* t = cx.runtime.spriteByName(name);
        if (t == nullptr) return false;
        *x = t->x;
        *y = t->y;
    }
    return true;
}

void motionGoTo(PrimitiveContext& cx) {
    double x, y;
    if (targetXY(cx, cx.input("TO"), &x, &y)) cx.runtime.setXY(cx.target(), x, y);
}

void glide(PrimitiveContext& cx, double secs, double x, double y) {
    ExecutionContext& ec = cx.stackFrame();
    Target& t = cx.target();
    if (ec.glideTimerStart) {
        const double elapsed = cx.runtime.now() - *ec.glideTimerStart;
        if (elapsed < ec.glideDuration * 1000) {
            const double frac = elapsed / (ec.glideDuration * 1000);
            const double dx = frac * (ec.glideEndX - ec.glideStartX);
            const double dy = frac * (ec.glideEndY - ec.glideStartY);
            cx.runtime.setXY(t, ec.glideStartX + dx, ec.glideStartY + dy);
            cx.yield();
        } else {
            cx.runtime.setXY(t, ec.glideEndX, ec.glideEndY);
        }
    } else {
        ec.glideTimerStart = cx.runtime.now();
        ec.glideDuration = secs;
        ec.glideStartX = t.x;
        ec.glideStartY = t.y;
        ec.glideEndX = x;
        ec.glideEndY = y;
        if (ec.glideDuration <= 0) {
            cx.runtime.setXY(t, ec.glideEndX, ec.glideEndY);
            return;
        }
        cx.yield();
    }
}

void motionGlide(PrimitiveContext& cx) {
    // The arguments are evaluated on every re-execution, like scratch-vm.
    const double secs = cx.input("SECS").toNumber(), x = cx.input("X").toNumber(), y = cx.input("Y").toNumber();
    glide(cx, secs, x, y);
}
void motionGlideTo(PrimitiveContext& cx) {
    double x, y;
    const double secs = cx.input("SECS").toNumber();
    if (targetXY(cx, cx.input("TO"), &x, &y)) glide(cx, secs, x, y);
}

void motionPointTowards(PrimitiveContext& cx) {
    const Value towards = cx.input("TOWARDS");
    double tx = 0, ty = 0;
    if (towards.isString() && towards.toString() == "_mouse_") {
        tx = cx.runtime.mouseX();
        ty = cx.runtime.mouseY();
    } else if (towards.isString() && towards.toString() == "_random_") {
        cx.runtime.setDirection(cx.target(), jsRound(cx.runtime.random() * 360) - 180);
        return;
    } else {
        Target* t = cx.runtime.spriteByName(towards.toString());
        if (t == nullptr) return;
        tx = t->x;
        ty = t->y;
    }
    const double dx = tx - cx.target().x;
    const double dy = ty - cx.target().y;
    cx.runtime.setDirection(cx.target(), 90 - radToDeg(std::atan2(dy, dx)));
}

void motionIfOnEdgeBounce(PrimitiveContext& cx) {
    Target& t = cx.target();
    Rect b;
    if (!cx.runtime.getBounds(t, &b)) return;
    const double W = 480, H = 360;
    const double distLeft = std::max(0.0, (W / 2) + b.left);
    const double distTop = std::max(0.0, (H / 2) - b.top);
    const double distRight = std::max(0.0, (W / 2) - b.right);
    const double distBottom = std::max(0.0, (H / 2) + b.bottom);
    std::string nearest;
    double minDist = std::numeric_limits<double>::infinity();
    if (distLeft < minDist) { minDist = distLeft; nearest = "left"; }
    if (distTop < minDist) { minDist = distTop; nearest = "top"; }
    if (distRight < minDist) { minDist = distRight; nearest = "right"; }
    if (distBottom < minDist) { minDist = distBottom; nearest = "bottom"; }
    if (minDist > 0) return;
    const double radians = degToRad(90 - t.direction);
    double dx = std::cos(radians);
    double dy = -std::sin(radians);
    if (nearest == "left") dx = std::max(0.2, std::fabs(dx));
    else if (nearest == "top") dy = std::max(0.2, std::fabs(dy));
    else if (nearest == "right") dx = 0 - std::max(0.2, std::fabs(dx));
    else if (nearest == "bottom") dy = 0 - std::max(0.2, std::fabs(dy));
    cx.runtime.setDirection(t, radToDeg(std::atan2(dy, dx)) + 90);
    double fx, fy;
    if (cx.runtime.keepInFence(t, t.x, t.y, &fx, &fy)) cx.runtime.setXY(t, fx, fy);
}

void motionSetRotationStyle(PrimitiveContext& cx) { cx.runtime.setRotationStyle(cx.target(), fieldString(cx, "STYLE")); }

// -------------------------------------------------------------------- sensing

Value sensingTouchingObject(PrimitiveContext& cx) {
    return Value::boolean(cx.runtime.isTouchingObject(cx.target(), cx.input("TOUCHINGOBJECTMENU")));
}

void toRgbList(const Value& v, int out[3]) {
    const Rgb c = toRgbColorObject(v);
    out[0] = static_cast<int>(c.r);
    out[1] = static_cast<int>(c.g);
    out[2] = static_cast<int>(c.b);
}

Value sensingTouchingColor(PrimitiveContext& cx) {
    int rgb[3];
    toRgbList(cx.input("COLOR"), rgb);
    Host* h = cx.runtime.host();
    return Value::boolean(h != nullptr && h->isTouchingColor(cx.target().drawableId, rgb));
}

Value sensingColorTouchingColor(PrimitiveContext& cx) {
    int mask[3], target[3];
    toRgbList(cx.input("COLOR"), mask);
    toRgbList(cx.input("COLOR2"), target);
    Host* h = cx.runtime.host();
    return Value::boolean(h != nullptr && h->colorIsTouchingColor(cx.target().drawableId, target, mask));
}

Value sensingDistanceTo(PrimitiveContext& cx) {
    if (cx.target().isStage) return Value::number(10000);
    const Value what = cx.input("DISTANCETOMENU");
    double tx = 0, ty = 0;
    if (what.isString() && what.toString() == "_mouse_") {
        tx = cx.runtime.mouseX();
        ty = cx.runtime.mouseY();
    } else {
        Target* t = cx.runtime.spriteByName(what.toString());
        if (t == nullptr) return Value::number(10000);
        tx = t->x;
        ty = t->y;
    }
    const double dx = cx.target().x - tx, dy = cx.target().y - ty;
    return Value::number(std::sqrt(dx * dx + dy * dy));
}

Value sensingTimer(PrimitiveContext& cx) { return Value::number(cx.runtime.projectTimer()); }
void sensingResetTimer(PrimitiveContext& cx) { cx.runtime.resetProjectTimer(); }
Value sensingMouseX(PrimitiveContext& cx) { return Value::number(cx.runtime.mouseX()); }
Value sensingMouseY(PrimitiveContext& cx) { return Value::number(cx.runtime.mouseY()); }
Value sensingMouseDown(PrimitiveContext& cx) { return Value::boolean(cx.runtime.mouseDown()); }
void sensingSetDragMode(PrimitiveContext& cx) { cx.target().draggable = fieldString(cx, "DRAG_MODE") == "draggable"; }
Value sensingKeyPressed(PrimitiveContext& cx) { return Value::boolean(cx.runtime.keyIsDown(cx.input("KEY_OPTION"))); }

Value sensingCurrent(PrimitiveContext& cx) {
    const std::string option = jsToLowerCase(fieldString(cx, "CURRENTMENU"));
    std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    if (option == "year") return Value::number(tm.tm_year + 1900);
    if (option == "month") return Value::number(tm.tm_mon + 1);
    if (option == "date") return Value::number(tm.tm_mday);
    if (option == "dayofweek") return Value::number(tm.tm_wday + 1);
    if (option == "hour") return Value::number(tm.tm_hour);
    if (option == "minute") return Value::number(tm.tm_min);
    if (option == "second") return Value::number(tm.tm_sec);
    return Value::number(0);
}

Value sensingDaysSince2000(PrimitiveContext&) {
    using namespace std::chrono;
    const double msPerDay = 24.0 * 60 * 60 * 1000;
    const double nowMs = static_cast<double>(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
    // new Date(2000, 0, 1) in local time
    std::tm start{};
    start.tm_year = 100;
    start.tm_mon = 0;
    start.tm_mday = 1;
    start.tm_isdst = -1;
    const std::time_t startT = std::mktime(&start);
    std::tm startLocal{};
#ifdef _WIN32
    localtime_s(&startLocal, &startT);
#else
    localtime_r(&startT, &startLocal);
#endif
    const std::time_t nowT = static_cast<std::time_t>(nowMs / 1000.0);
    std::tm nowLocal{};
#ifdef _WIN32
    localtime_s(&nowLocal, &nowT);
#else
    localtime_r(&nowT, &nowLocal);
#endif
    auto offsetMinutes = [](const std::tm& tm, std::time_t t) {
        // JavaScript's getTimezoneOffset: minutes UTC is ahead of local time.
        std::tm gm{};
#ifdef _WIN32
        gmtime_s(&gm, &t);
#else
        gmtime_r(&t, &gm);
#endif
        gm.tm_isdst = tm.tm_isdst;
        return (static_cast<double>(std::mktime(&gm)) - static_cast<double>(t)) / 60.0 * -1.0 * -1.0;
    };
    (void)offsetMinutes;
    // Offsets via tm_gmtoff where available.
    const double startOffset = -static_cast<double>(startLocal.tm_gmtoff) / 60.0;
    const double nowOffset = -static_cast<double>(nowLocal.tm_gmtoff) / 60.0;
    const double dstAdjust = nowOffset - startOffset;
    double ms = nowMs - static_cast<double>(startT) * 1000.0;
    ms += (nowOffset - dstAdjust) * 60 * 1000;
    return Value::number(ms / msPerDay);
}

Value sensingLoudness(PrimitiveContext&) { return Value::number(-1); }
Value sensingLoud(PrimitiveContext&) { return Value::boolean(false); }

void sensingAskAndWait(PrimitiveContext& cx) {
    cx.runtime.askAndWait(cx.thread, cx.input("QUESTION").toString());
}
Value sensingAnswer(PrimitiveContext& cx) { return Value::string(cx.runtime.answer()); }
Value sensingUsername(PrimitiveContext&) { return Value::string(""); }

Value sensingOf(PrimitiveContext& cx) {
    const Value obj = cx.input("OBJECT");
    Target* t = nullptr;
    if (obj.isString() && obj.toString() == "_stage_") {
        t = cx.runtime.stageTarget();
    } else {
        t = cx.runtime.spriteByName(obj.toString());
    }
    if (t == nullptr) return Value::number(0);
    const std::string prop = fieldString(cx, "PROPERTY");
    if (t->isStage) {
        if (prop == "background #" || prop == "backdrop #") return Value::number(t->currentCostume + 1);
        if (prop == "backdrop name") return costumeNumberName(*t, "name");
        if (prop == "volume") return Value::number(t->volume);
    } else {
        if (prop == "x position") return Value::number(t->x);
        if (prop == "y position") return Value::number(t->y);
        if (prop == "direction") return Value::number(t->direction);
        if (prop == "costume #") return Value::number(t->currentCostume + 1);
        if (prop == "costume name") return costumeNumberName(*t, "name");
        if (prop == "size") return Value::number(t->size);
        if (prop == "volume") return Value::number(t->volume);
    }
    // Variable of that target (its own variables only).
    for (const Variable& v : t->variables) {
        if (v.type == Variable::Type::Scalar && v.name == prop) return v.value;
    }
    return Value::number(0);
}

// --------------------------------------------------------------------- events

Value hatGreaterThan(PrimitiveContext& cx) {
    const std::string option = jsToLowerCase(fieldString(cx, "WHENGREATERTHANMENU"));
    const double value = cx.input("VALUE").toNumber();
    if (option == "timer") return Value::boolean(cx.runtime.projectTimer() > value);
    return Value::boolean(false);  // loudness: no audio engine
}
Value hatTouchingObject(PrimitiveContext& cx) {
    return Value::boolean(cx.runtime.isTouchingObject(cx.target(), cx.input("TOUCHINGOBJECTMENU")));
}

// ---------------------------------------------------------------------- sound

int soundIndexOf(Target& t, const Value& arg) {
    const auto& sounds = t.assets->sounds;
    if (sounds.empty()) return -1;
    const std::string name = arg.toString();
    if (arg.isString()) {
        for (std::size_t i = 0; i < sounds.size(); i++) {
            if (sounds[i].name == name) return static_cast<int>(i);
        }
    }
    const double idx = jsParseInt(name);
    if (!std::isnan(idx)) {
        const double n = static_cast<double>(sounds.size()) - 1;
        const double range = n + 1;
        const double v = (idx - 1) - (std::floor((idx - 1) / range) * range);
        return static_cast<int>(v);
    }
    return -1;
}

void soundPlay(PrimitiveContext& cx) {
    const int idx = soundIndexOf(cx.target(), cx.input("SOUND_MENU"));
    if (idx >= 0) cx.runtime.playSound(nullptr, cx.target(), idx);
}
void soundPlayUntilDone(PrimitiveContext& cx) {
    const int idx = soundIndexOf(cx.target(), cx.input("SOUND_MENU"));
    if (idx >= 0) cx.runtime.playSound(&cx.thread, cx.target(), idx);
}
void soundStopAll(PrimitiveContext& cx) { cx.runtime.stopAllSounds(); }

void soundUpdateEffect(PrimitiveContext& cx, bool change) {
    const std::string effect = jsToLowerCase(fieldString(cx, "EFFECT"));
    const double value = cx.input("VALUE").toNumber();
    double* slot = effect == "pitch" ? &cx.target().soundPitch : effect == "pan" ? &cx.target().soundPan : nullptr;
    if (slot == nullptr) return;
    *slot = change ? *slot + value : value;
    if (effect == "pitch") *slot = clampd(*slot, -360, 360);
    else *slot = clampd(*slot, -100, 100);
    cx.runtime.syncSoundState(cx.target());
    cx.runtime.waitForPromise(cx.thread, Runtime::PromiseKind::Immediate);
}
void soundSetEffect(PrimitiveContext& cx) { soundUpdateEffect(cx, false); }
void soundChangeEffect(PrimitiveContext& cx) { soundUpdateEffect(cx, true); }
void soundClearEffects(PrimitiveContext& cx) {
    cx.target().soundPitch = 0;
    cx.target().soundPan = 0;
    cx.runtime.syncSoundState(cx.target());
}
void soundUpdateVolume(PrimitiveContext& cx, double volume) {
    cx.target().volume = clampd(volume, 0, 100);
    cx.runtime.syncSoundState(cx.target());
    cx.runtime.waitForPromise(cx.thread, Runtime::PromiseKind::Immediate);
}
void soundSetVolume(PrimitiveContext& cx) { soundUpdateVolume(cx, cx.input("VOLUME").toNumber()); }
void soundChangeVolume(PrimitiveContext& cx) { soundUpdateVolume(cx, cx.input("VOLUME").toNumber() + cx.target().volume); }
Value soundVolume(PrimitiveContext& cx) { return Value::number(cx.target().volume); }

// ------------------------------------------------------------------------ pen

void updatePenColor(PenState& s) {
    const Rgb rgb = hsvToRgb({s.color * 360 / 100, s.saturation / 100, s.brightness / 100});
    s.color4f[0] = rgb.r / 255.0;
    s.color4f[1] = rgb.g / 255.0;
    s.color4f[2] = rgb.b / 255.0;
    s.color4f[3] = 1.0 - (s.transparency / 100.0);
}

double wrapColor(double v) {
    const double range = 101;
    return v - (std::floor(v / range) * range);
}

void setOrChangeColorParam(const std::string& param, double value, PenState& s, bool change) {
    if (param == "color") s.color = wrapColor(value + (change ? s.color : 0));
    else if (param == "saturation") s.saturation = clampd(value + (change ? s.saturation : 0), 0, 100);
    else if (param == "brightness") s.brightness = clampd(value + (change ? s.brightness : 0), 0, 100);
    else if (param == "transparency") s.transparency = clampd(value + (change ? s.transparency : 0), 0, 100);
    updatePenColor(s);
}

void legacyUpdatePenColor(PenState& s) {
    Rgb rgb = hsvToRgb({s.color * 360 / 100, 1, 1});
    const double shade = (s.shade > 100) ? 200 - s.shade : s.shade;
    if (shade < 50) {
        rgb = mixRgb({0, 0, 0}, rgb, (10 + shade) / 60);
    } else {
        rgb = mixRgb(rgb, {255, 255, 255}, (shade - 50) / 60);
    }
    const Hsv hsv = rgbToHsv(rgb);
    s.color = 100 * hsv.h / 360;
    s.saturation = 100 * hsv.s;
    s.brightness = 100 * hsv.v;
    updatePenColor(s);
}

void penClear(PrimitiveContext& cx) {
    if (cx.runtime.host() != nullptr) {
        cx.runtime.host()->penClear();
        cx.runtime.requestRedraw();
    }
}
void penStamp(PrimitiveContext& cx) {
    if (cx.runtime.host() != nullptr) {
        cx.runtime.host()->penStamp(cx.target().drawableId);
        cx.runtime.requestRedraw();
    }
}
void penDown(PrimitiveContext& cx) {
    Target& t = cx.target();
    t.pen.down = true;
    if (cx.runtime.host() != nullptr) {
        cx.runtime.host()->penPoint(t.pen.color4f, t.pen.diameter, t.x, t.y);
        cx.runtime.requestRedraw();
    }
}
void penUp(PrimitiveContext& cx) { cx.target().pen.down = false; }

void penSetColorToColor(PrimitiveContext& cx) {
    PenState& s = cx.target().pen;
    const Rgb rgb = toRgbColorObject(cx.input("COLOR"));
    const Hsv hsv = rgbToHsv(rgb);
    s.color = (hsv.h / 360) * 100;
    s.saturation = hsv.s * 100;
    s.brightness = hsv.v * 100;
    s.transparency = rgb.hasAlpha ? 100 * (1 - (rgb.a / 255.0)) : 0;
    s.shade = s.brightness / 2;
    updatePenColor(s);
}
void penChangeColorParam(PrimitiveContext& cx) {
    setOrChangeColorParam(cx.input("COLOR_PARAM").toString(), cx.input("VALUE").toNumber(), cx.target().pen, true);
}
void penSetColorParam(PrimitiveContext& cx) {
    setOrChangeColorParam(cx.input("COLOR_PARAM").toString(), cx.input("VALUE").toNumber(), cx.target().pen, false);
}
void penChangeSize(PrimitiveContext& cx) {
    PenState& s = cx.target().pen;
    s.diameter = clampd(s.diameter + cx.input("SIZE").toNumber(), 1, 1200);
}
void penSetSize(PrimitiveContext& cx) {
    cx.target().pen.diameter = clampd(cx.input("SIZE").toNumber(), 1, 1200);
}
void penSetShade(PenState& s, double shade) {
    shade = std::fmod(shade, 200.0);
    if (shade < 0) shade += 200;
    s.shade = shade;
    legacyUpdatePenColor(s);
}
void penSetShadeTo(PrimitiveContext& cx) { penSetShade(cx.target().pen, cx.input("SHADE").toNumber()); }
void penChangeShadeBy(PrimitiveContext& cx) {
    PenState& s = cx.target().pen;
    penSetShade(s, s.shade + cx.input("SHADE").toNumber());
}
void penSetHueTo(PrimitiveContext& cx) {
    PenState& s = cx.target().pen;
    setOrChangeColorParam("color", cx.input("HUE").toNumber() / 2, s, false);
    setOrChangeColorParam("transparency", 0, s, false);
    legacyUpdatePenColor(s);
}
void penChangeHueBy(PrimitiveContext& cx) {
    PenState& s = cx.target().pen;
    setOrChangeColorParam("color", cx.input("HUE").toNumber() / 2, s, true);
    legacyUpdatePenColor(s);
}

// -------------------------------------------------------------- control extras

void controlAllAtOnce(PrimitiveContext& cx) { cx.startBranch(1, false); }
Value controlGetCounter(PrimitiveContext& cx) { return Value::number(cx.runtime.counter); }
void controlIncrCounter(PrimitiveContext& cx) { cx.runtime.counter++; }
void controlClearCounter(PrimitiveContext& cx) { cx.runtime.counter = 0; }

const Primitive kExt[] = {
    // looks
    {"looks_say", Kind::Command, &lookSay, nullptr, false},
    {"looks_sayforsecs", Kind::Command, &lookSayForSecs, nullptr, false},
    {"looks_think", Kind::Command, &lookThink, nullptr, false},
    {"looks_thinkforsecs", Kind::Command, &lookThinkForSecs, nullptr, false},
    {"looks_show", Kind::Command, &lookShow, nullptr, false},
    {"looks_hide", Kind::Command, &lookHide, nullptr, false},
    {"looks_hideallsprites", Kind::Command, &noopCommand, nullptr, false},
    {"looks_switchcostumeto", Kind::Command, &lookSwitchCostume, nullptr, false},
    {"looks_switchbackdropto", Kind::Command, &lookSwitchBackdrop, nullptr, false},
    {"looks_switchbackdroptoandwait", Kind::Command, &lookSwitchBackdropAndWait, nullptr, false},
    {"looks_nextcostume", Kind::Command, &lookNextCostume, nullptr, false},
    {"looks_nextbackdrop", Kind::Command, &lookNextBackdrop, nullptr, false},
    {"looks_changeeffectby", Kind::Command, &lookChangeEffect, nullptr, false},
    {"looks_seteffectto", Kind::Command, &lookSetEffect, nullptr, false},
    {"looks_cleargraphiceffects", Kind::Command, &lookClearEffects, nullptr, false},
    {"looks_changesizeby", Kind::Command, &lookChangeSize, nullptr, false},
    {"looks_setsizeto", Kind::Command, &lookSetSize, nullptr, false},
    {"looks_changestretchby", Kind::Command, &noopCommand, nullptr, false},
    {"looks_setstretchto", Kind::Command, &noopCommand, nullptr, false},
    {"looks_gotofrontback", Kind::Command, &lookGoToFrontBack, nullptr, false},
    {"looks_goforwardbackwardlayers", Kind::Command, &lookGoForwardBackward, nullptr, false},
    {"looks_size", Kind::Reporter, nullptr, &lookSize, false},
    {"looks_costumenumbername", Kind::Reporter, nullptr, &lookCostumeNumberName, false},
    {"looks_backdropnumbername", Kind::Reporter, nullptr, &lookBackdropNumberName, false},
    {"looks_costume", Kind::Reporter, nullptr, &menuReporter, false},
    {"looks_backdrops", Kind::Reporter, nullptr, &menuReporter, false},
    // motion
    {"motion_goto", Kind::Command, &motionGoTo, nullptr, false},
    {"motion_goto_menu", Kind::Reporter, nullptr, &menuReporter, false},
    {"motion_glidesecstoxy", Kind::Command, &motionGlide, nullptr, false},
    {"motion_glideto", Kind::Command, &motionGlideTo, nullptr, false},
    {"motion_glideto_menu", Kind::Reporter, nullptr, &menuReporter, false},
    {"motion_pointtowards", Kind::Command, &motionPointTowards, nullptr, false},
    {"motion_pointtowards_menu", Kind::Reporter, nullptr, &menuReporter, false},
    {"motion_ifonedgebounce", Kind::Command, &motionIfOnEdgeBounce, nullptr, false},
    {"motion_setrotationstyle", Kind::Command, &motionSetRotationStyle, nullptr, false},
    {"motion_scroll_right", Kind::Command, &noopCommand, nullptr, false},
    {"motion_scroll_up", Kind::Command, &noopCommand, nullptr, false},
    {"motion_align_scene", Kind::Command, &noopCommand, nullptr, false},
    // sensing
    {"sensing_touchingobject", Kind::Reporter, nullptr, &sensingTouchingObject, false},
    {"sensing_touchingobjectmenu", Kind::Reporter, nullptr, &menuReporter, false},
    {"sensing_touchingcolor", Kind::Reporter, nullptr, &sensingTouchingColor, false},
    {"sensing_coloristouchingcolor", Kind::Reporter, nullptr, &sensingColorTouchingColor, false},
    {"sensing_distanceto", Kind::Reporter, nullptr, &sensingDistanceTo, false},
    {"sensing_distancetomenu", Kind::Reporter, nullptr, &menuReporter, false},
    {"sensing_timer", Kind::Reporter, nullptr, &sensingTimer, false},
    {"sensing_resettimer", Kind::Command, &sensingResetTimer, nullptr, false},
    {"sensing_of", Kind::Reporter, nullptr, &sensingOf, false},
    {"sensing_of_object_menu", Kind::Reporter, nullptr, &menuReporter, false},
    {"sensing_mousex", Kind::Reporter, nullptr, &sensingMouseX, false},
    {"sensing_mousey", Kind::Reporter, nullptr, &sensingMouseY, false},
    {"sensing_mousedown", Kind::Reporter, nullptr, &sensingMouseDown, false},
    {"sensing_setdragmode", Kind::Command, &sensingSetDragMode, nullptr, false},
    {"sensing_keypressed", Kind::Reporter, nullptr, &sensingKeyPressed, false},
    {"sensing_keyoptions", Kind::Reporter, nullptr, &menuReporter, false},
    {"sensing_current", Kind::Reporter, nullptr, &sensingCurrent, false},
    {"sensing_dayssince2000", Kind::Reporter, nullptr, &sensingDaysSince2000, false},
    {"sensing_loudness", Kind::Reporter, nullptr, &sensingLoudness, false},
    {"sensing_loud", Kind::Reporter, nullptr, &sensingLoud, false},
    {"sensing_askandwait", Kind::Command, &sensingAskAndWait, nullptr, false},
    {"sensing_answer", Kind::Reporter, nullptr, &sensingAnswer, false},
    {"sensing_username", Kind::Reporter, nullptr, &sensingUsername, false},
    // events
    {"event_whenkeypressed", Kind::Hat, nullptr, nullptr, false},
    {"event_whenthisspriteclicked", Kind::Hat, nullptr, nullptr, true},
    {"event_whenstageclicked", Kind::Hat, nullptr, nullptr, true},
    {"event_whenbackdropswitchesto", Kind::Hat, nullptr, nullptr, true},
    {"event_whengreaterthan", Kind::Hat, nullptr, &hatGreaterThan, false, true},
    {"event_whentouchingobject", Kind::Hat, nullptr, &hatTouchingObject, false, true},
    {"event_touchingobjectmenu", Kind::Reporter, nullptr, &menuReporter, false},
    // sound
    {"sound_play", Kind::Command, &soundPlay, nullptr, false},
    {"sound_playuntildone", Kind::Command, &soundPlayUntilDone, nullptr, false},
    {"sound_stopallsounds", Kind::Command, &soundStopAll, nullptr, false},
    {"sound_seteffectto", Kind::Command, &soundSetEffect, nullptr, false},
    {"sound_changeeffectby", Kind::Command, &soundChangeEffect, nullptr, false},
    {"sound_cleareffects", Kind::Command, &soundClearEffects, nullptr, false},
    {"sound_sounds_menu", Kind::Reporter, nullptr, &menuReporter, false},
    {"sound_setvolumeto", Kind::Command, &soundSetVolume, nullptr, false},
    {"sound_changevolumeby", Kind::Command, &soundChangeVolume, nullptr, false},
    {"sound_volume", Kind::Reporter, nullptr, &soundVolume, false},
    // pen
    {"pen_clear", Kind::Command, &penClear, nullptr, false},
    {"pen_stamp", Kind::Command, &penStamp, nullptr, false},
    {"pen_penDown", Kind::Command, &penDown, nullptr, false},
    {"pen_penUp", Kind::Command, &penUp, nullptr, false},
    {"pen_setPenColorToColor", Kind::Command, &penSetColorToColor, nullptr, false},
    {"pen_changePenColorParamBy", Kind::Command, &penChangeColorParam, nullptr, false},
    {"pen_setPenColorParamTo", Kind::Command, &penSetColorParam, nullptr, false},
    {"pen_changePenSizeBy", Kind::Command, &penChangeSize, nullptr, false},
    {"pen_setPenSizeTo", Kind::Command, &penSetSize, nullptr, false},
    {"pen_setPenShadeToNumber", Kind::Command, &penSetShadeTo, nullptr, false},
    {"pen_changePenShadeBy", Kind::Command, &penChangeShadeBy, nullptr, false},
    {"pen_setPenHueToNumber", Kind::Command, &penSetHueTo, nullptr, false},
    {"pen_changePenHueBy", Kind::Command, &penChangeHueBy, nullptr, false},
    {"pen_menu_colorParam", Kind::Reporter, nullptr, &menuReporter, false},
    // control extras
    {"control_all_at_once", Kind::Command, &controlAllAtOnce, nullptr, false},
    {"control_get_counter", Kind::Reporter, nullptr, &controlGetCounter, false},
    {"control_incr_counter", Kind::Command, &controlIncrCounter, nullptr, false},
    {"control_clear_counter", Kind::Command, &controlClearCounter, nullptr, false},
    // data: monitor visibility only
    {"data_showvariable", Kind::Command, &noopCommand, nullptr, false},
    {"data_hidevariable", Kind::Command, &noopCommand, nullptr, false},
    {"data_showlist", Kind::Command, &noopCommand, nullptr, false},
    {"data_hidelist", Kind::Command, &noopCommand, nullptr, false},
};

}  // namespace

void registerExtPrimitives(PrimitiveMap& map) {
    for (const Primitive& p : kExt) map.emplace(p.opcode, &p);
}

}  // namespace lithium

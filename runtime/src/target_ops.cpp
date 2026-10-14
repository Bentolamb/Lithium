// RenderedTarget behaviour ported from scratch-vm (sprites/rendered-target.js),
// plus the runtime-level input devices and promise machinery.
#include <algorithm>
#include <cmath>
#include <limits>

#include "lithium/runtime.h"
#include "lithium/text.h"
#include "sequencer.h"

namespace lithium {

namespace {

constexpr double kStageWidth = 480.0;
constexpr double kStageHeight = 360.0;

double wrapClamp(double n, double min, double max) {
    const double range = (max - min) + 1;
    return n - (std::floor((n - min) / range) * range);
}

double clampd(double n, double min, double max) { return std::min(std::max(n, min), max); }

}  // namespace

// ------------------------------------------------------------------ host/drawables

void Runtime::setHost(Host* host) {
    host_ = host;
    if (host_ != nullptr) syncRenderer();
}

void Runtime::initDrawable(Target& t) {
    if (host_ == nullptr) return;
    t.drawableId = host_->createDrawable(t.isStage ? kBackgroundLayer : kSpriteLayer);
}

void Runtime::updateAllDrawableProperties(Target& t) {
    if (host_ == nullptr || t.drawableId < 0) return;
    double dir, sx, sy;
    renderedDirectionAndScale(t, &dir, &sx, &sy);
    host_->updateDrawablePosition(t.drawableId, t.x, t.y);
    host_->updateDrawableDirectionScale(t.drawableId, dir, sx, sy);
    host_->updateDrawableVisible(t.drawableId, t.visible);
    const auto& costumes = t.costumes();
    if (t.currentCostume >= 0 && static_cast<std::size_t>(t.currentCostume) < costumes.size()) {
        host_->updateDrawableSkinId(t.drawableId, costumes[static_cast<std::size_t>(t.currentCostume)].skinId);
    }
    host_->updateDrawableEffect(t.drawableId, "color", t.effects.color);
    host_->updateDrawableEffect(t.drawableId, "fisheye", t.effects.fisheye);
    host_->updateDrawableEffect(t.drawableId, "whirl", t.effects.whirl);
    host_->updateDrawableEffect(t.drawableId, "pixelate", t.effects.pixelate);
    host_->updateDrawableEffect(t.drawableId, "mosaic", t.effects.mosaic);
    host_->updateDrawableEffect(t.drawableId, "brightness", t.effects.brightness);
    host_->updateDrawableEffect(t.drawableId, "ghost", t.effects.ghost);
    if (t.visible) requestRedraw();
}

void Runtime::syncRenderer() {
    if (host_ == nullptr) return;
    for (const auto& t : project_.targets()) {
        if (t->drawableId >= 0) host_->destroyDrawable(t->drawableId, t->isStage ? kBackgroundLayer : kSpriteLayer);
        t->drawableId = -1;
    }
    for (const auto& t : project_.targets()) {
        initDrawable(*t);
        updateAllDrawableProperties(*t);
    }
}

Target* Runtime::stageTarget() { return &project_.stage(); }

Target* Runtime::spriteByName(std::string_view name) {
    for (const auto& t : project_.targets()) {
        if (!t->isStage && t->isOriginal && t->name == name) return t.get();
    }
    return nullptr;
}

Target* Runtime::targetByUid(int uid) {
    for (const auto& t : project_.targets()) {
        if (t->uid == uid) return t.get();
    }
    for (const auto& t : graveyard_) {
        if (t->uid == uid) return t.get();
    }
    return nullptr;
}

void Runtime::renderedDirectionAndScale(const Target& t, double* direction, double* sx, double* sy) const {
    *direction = t.direction;
    *sx = t.size;
    *sy = t.size;
    if (t.rotationStyle == RotationStyle::DontRotate) {
        *direction = 90;
    } else if (t.rotationStyle == RotationStyle::LeftRight) {
        *direction = 90;
        const double flip = t.direction < 0 ? -1 : 1;
        *sx = flip * t.size;
    }
}

// ---------------------------------------------------------------------- setters

void Runtime::setXY(Target& t, double x, double y, bool force) {
    if (t.isStage) return;
    const double oldX = t.x, oldY = t.y;
    if (host_ != nullptr) {
        double fx = x, fy = y;
        host_->getFencedPosition(t.drawableId, x, y, &fx, &fy);
        t.x = fx;
        t.y = fy;
        host_->updateDrawablePosition(t.drawableId, fx, fy);
        if (t.visible) requestRedraw();
    } else {
        t.x = x;
        t.y = y;
        if (rendererAttached_ && t.visible) requestRedraw();
    }
    // EVENT_TARGET_MOVED: the pen listener draws a line while the pen is down.
    if (t.pen.down && !force && host_ != nullptr) {
        host_->penLine(t.pen.color4f, t.pen.diameter, oldX, oldY, t.x, t.y);
        requestRedraw();
    }
}

void Runtime::setDirection(Target& t, double direction) {
    if (t.isStage) return;
    if (!std::isfinite(direction)) return;
    t.direction = wrapClamp(direction, -179, 180);
    if (host_ != nullptr) {
        double d, sx, sy;
        renderedDirectionAndScale(t, &d, &sx, &sy);
        host_->updateDrawableDirectionScale(t.drawableId, d, sx, sy);
    }
    if ((host_ != nullptr || rendererAttached_) && t.visible) requestRedraw();
}

void Runtime::setVisible(Target& t, bool visible) {
    if (t.isStage) return;
    t.visible = visible;
    if (host_ != nullptr) host_->updateDrawableVisible(t.drawableId, t.visible);
    if ((host_ != nullptr || rendererAttached_) && t.visible) requestRedraw();
}

void Runtime::setSize(Target& t, double size) {
    if (t.isStage) return;
    if (host_ == nullptr) return;  // scratch-vm only resizes with a renderer
    double w = 0, h = 0;
    host_->getCurrentSkinSize(t.drawableId, &w, &h);
    const double minScale = std::min(1.0, std::max(5 / w, 5 / h));
    const double maxScale = std::min((1.5 * kStageWidth) / w, (1.5 * kStageHeight) / h);
    t.size = clampd(size / 100, minScale, maxScale) * 100;
    double d, sx, sy;
    renderedDirectionAndScale(t, &d, &sx, &sy);
    host_->updateDrawableDirectionScale(t.drawableId, d, sx, sy);
    if (t.visible) requestRedraw();
}

void Runtime::setEffect(Target& t, std::string_view name, double value) {
    double* slot = t.effects.byName(name);
    if (slot == nullptr) return;
    *slot = value;
    if (host_ != nullptr) {
        host_->updateDrawableEffect(t.drawableId, std::string(name).c_str(), value);
        if (t.visible) requestRedraw();
    }
}

void Runtime::clearEffects(Target& t) {
    t.effects.clear();
    if (host_ != nullptr) {
        for (const char* n : {"color", "fisheye", "whirl", "pixelate", "mosaic", "brightness", "ghost"}) {
            host_->updateDrawableEffect(t.drawableId, n, 0);
        }
        if (t.visible) requestRedraw();
    }
}

void Runtime::setCostume(Target& t, double index) {
    index = jsRound(index);
    if (!std::isfinite(index)) index = 0;
    const auto& costumes = t.costumes();
    if (costumes.empty()) {
        t.currentCostume = 0;
    } else {
        t.currentCostume = static_cast<int>(wrapClamp(index, 0, static_cast<double>(costumes.size()) - 1));
    }
    if (host_ != nullptr && !costumes.empty()) {
        host_->updateDrawableSkinId(t.drawableId, costumes[static_cast<std::size_t>(t.currentCostume)].skinId);
        if (t.visible) requestRedraw();
    }
}

void Runtime::setRotationStyle(Target& t, std::string_view style) {
    if (style == "don't rotate") {
        t.rotationStyle = RotationStyle::DontRotate;
    } else if (style == "all around") {
        t.rotationStyle = RotationStyle::AllAround;
    } else if (style == "left-right") {
        t.rotationStyle = RotationStyle::LeftRight;
    }
    if (host_ != nullptr) {
        double d, sx, sy;
        renderedDirectionAndScale(t, &d, &sx, &sy);
        host_->updateDrawableDirectionScale(t.drawableId, d, sx, sy);
        if (t.visible) requestRedraw();
    }
}

// ------------------------------------------------------------------ layer order

namespace {

// Runtime.moveExecutable.
double moveExecutableImpl(Project& project, Target& t, double delta) {
    auto& list = project.mutableTargets();
    std::size_t oldIndex = list.size();
    for (std::size_t i = 0; i < list.size(); i++) {
        if (list[i].get() == &t) oldIndex = i;
    }
    if (oldIndex == list.size()) return -1;
    std::unique_ptr<Target> owned = std::move(list[oldIndex]);
    list.erase(list.begin() + static_cast<std::ptrdiff_t>(oldIndex));
    double newIndex = static_cast<double>(oldIndex) + delta;
    if (newIndex > static_cast<double>(list.size())) newIndex = static_cast<double>(list.size());
    if (newIndex <= 0) {
        newIndex = (!list.empty() && list[0]->isStage) ? 1 : 0;
    }
    std::size_t at = std::isnan(newIndex) ? 0 : static_cast<std::size_t>(std::trunc(newIndex));
    list.insert(list.begin() + static_cast<std::ptrdiff_t>(at), std::move(owned));
    return static_cast<double>(at);
}

}  // namespace

void Runtime::goToFront(Target& t) {
    if (host_ != nullptr) host_->setDrawableOrder(t.drawableId, std::numeric_limits<double>::infinity(), kSpriteLayer, false, 0);
    // setExecutablePosition(t, Infinity)
    moveExecutableImpl(project_, t, std::numeric_limits<double>::infinity());
}

void Runtime::goToBack(Target& t) {
    if (host_ != nullptr) host_->setDrawableOrder(t.drawableId, -std::numeric_limits<double>::infinity(), kSpriteLayer, false, 0);
    moveExecutableImpl(project_, t, -std::numeric_limits<double>::infinity());
}

void Runtime::goForwardLayers(Target& t, double n) {
    if (host_ != nullptr) host_->setDrawableOrder(t.drawableId, n, kSpriteLayer, true, 0);
    moveExecutableImpl(project_, t, n);
}

void Runtime::goBackwardLayers(Target& t, double n) {
    if (host_ != nullptr) host_->setDrawableOrder(t.drawableId, -n, kSpriteLayer, true, 0);
    moveExecutableImpl(project_, t, -n);
}

void Runtime::goBehindOther(Target& t, Target& other) {
    if (host_ != nullptr) {
        const double otherLayer = host_->setDrawableOrder(other.drawableId, 0, kSpriteLayer, true, 0);
        host_->setDrawableOrder(t.drawableId, otherLayer, kSpriteLayer, false, 0);
    }
    // setExecutablePosition(t, indexOf(other)): handled by insertBefore for fresh clones.
}

// ------------------------------------------------------------------- geometry

bool Runtime::getBounds(Target& t, Rect* out) {
    if (host_ == nullptr || t.drawableId < 0) return false;
    return host_->getBounds(t.drawableId, out);
}

bool Runtime::keepInFence(Target& t, double newX, double newY, double* outX, double* outY) {
    Rect b;
    if (!getBounds(t, &b)) return false;
    const double left = -kStageWidth / 2, right = kStageWidth / 2, top = kStageHeight / 2, bottom = -kStageHeight / 2;
    b.left += newX - t.x;
    b.right += newX - t.x;
    b.top += newY - t.y;
    b.bottom += newY - t.y;
    double dx = 0, dy = 0;
    if (b.left < left) dx += left - b.left;
    if (b.right > right) dx += right - b.right;
    if (b.top > top) dy += top - b.top;
    if (b.bottom < bottom) dy += bottom - b.bottom;
    *outX = newX + dx;
    *outY = newY + dy;
    return true;
}

bool Runtime::isTouchingSprite(Target& t, const std::string& spriteName) {
    Target* first = spriteByName(spriteName);
    if (first == nullptr || host_ == nullptr) return false;
    std::vector<int> ids;
    for (const auto& c : project_.targets()) {
        if (!c->isStage && c->name == first->name) ids.push_back(c->drawableId);
    }
    return host_->isTouchingDrawables(t.drawableId, ids.data(), static_cast<int>(ids.size()));
}

bool Runtime::isTouchingObject(Target& t, const Value& what) {
    const std::string s = what.toString();
    if (s == "_mouse_") {
        if (host_ == nullptr) return false;
        return host_->drawableTouching(t.drawableId, mouseClientX_, mouseClientY_);
    }
    if (s == "_edge_") {
        if (host_ == nullptr) return false;
        Rect b;
        if (!getBounds(t, &b)) return false;
        return b.left < -kStageWidth / 2 || b.right > kStageWidth / 2 || b.top > kStageHeight / 2 ||
               b.bottom < -kStageHeight / 2;
    }
    return isTouchingSprite(t, s);
}

// ---------------------------------------------------------------------- bubbles

void Runtime::setBubble(Target& t, const std::string& type, const Value& message) {
    t.bubble.type = type;
    // _formatBubbleText
    std::string text;
    if (message.isNumber()) {
        const double n = message.toNumber();
        if (std::fabs(n) >= 0.01 && std::fmod(n, 1.0) != 0.0) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "%.2f", n);
            text = buf;
        } else {
            text = message.toString();
        }
    } else {
        text = message.toString();
    }
    // substr(0, 330) in UTF-16 code units
    std::u16string wide = utf8ToUtf16(text);
    if (wide.size() > 330) wide.resize(330);
    text = utf16ToUtf8(wide);
    t.bubble.text = text;
    t.bubble.usageId = nextUsageId_++;
    if (host_ != nullptr) host_->setBubble(t.uid, t.drawableId, t.bubble.type.c_str(), t.visible ? text.c_str() : "");
}

void Runtime::clearBubble(Target& t) {
    t.bubble.text.clear();
    if (host_ != nullptr) host_->setBubble(t.uid, t.drawableId, t.bubble.type.c_str(), "");
}

void Runtime::syncSoundState(Target& t) {
    if (host_ != nullptr) host_->setSoundEffects(t.uid, t.volume, t.soundPitch, t.soundPan);
}

// ------------------------------------------------------------------------ input

namespace {

const char* kKeyNames[] = {"space", "left arrow", "up arrow", "right arrow", "down arrow", "enter"};

std::string keyStringToScratchKey(const std::string& key) {
    if (key == " ") return "space";
    if (key == "ArrowLeft" || key == "Left") return "left arrow";
    if (key == "ArrowUp" || key == "Up") return "up arrow";
    if (key == "Right" || key == "ArrowRight") return "right arrow";
    if (key == "Down" || key == "ArrowDown") return "down arrow";
    if (key == "Enter") return "enter";
    if (utf8ToUtf16(key).size() > 1) return "";
    return jsToUpperCase(key);
}

std::string keyArgToScratchKey(const Value& arg) {
    if (arg.isNumber()) {
        const double k = arg.toNumber();
        if (k >= 48 && k <= 90) return std::string(1, static_cast<char>(k));
        if (k == 32) return "space";
        if (k == 37) return "left arrow";
        if (k == 38) return "up arrow";
        if (k == 39) return "right arrow";
        if (k == 40) return "down arrow";
    }
    std::string s = arg.toString();
    for (const char* n : kKeyNames) {
        if (s == n) return s;
    }
    std::u16string wide = utf8ToUtf16(s);
    if (wide.size() > 1) s = utf16ToUtf8(wide.substr(0, 1));
    if (s == " ") return "space";
    return jsToUpperCase(s);
}

}  // namespace

void Runtime::postKey(const std::string& key, bool isDown) {
    if (key.empty()) return;
    const std::string scratchKey = keyStringToScratchKey(key);
    if (scratchKey.empty()) return;
    const auto it = std::find(keysPressed_.begin(), keysPressed_.end(), scratchKey);
    if (isDown) {
        // KEY_PRESSED: start the key hats.
        const HatField byKey{"KEY_OPTION", scratchKey};
        startHats("event_whenkeypressed", &byKey);
        const HatField any{"KEY_OPTION", "any"};
        startHats("event_whenkeypressed", &any);
        if (it == keysPressed_.end()) keysPressed_.push_back(scratchKey);
    } else if (it != keysPressed_.end()) {
        keysPressed_.erase(it);
    }
}

bool Runtime::keyIsDown(const Value& keyArg) const {
    if (keyArg.isString() && keyArg.toString() == "any") return !keysPressed_.empty();
    const std::string k = keyArgToScratchKey(keyArg);
    return std::find(keysPressed_.begin(), keysPressed_.end(), k) != keysPressed_.end();
}

void Runtime::postMouse(const MouseEvent& e) {
    // Like scratch-vm, a coordinate of exactly 0 is ignored (`if (data.x)`).
    if (e.hasX && e.x != 0) {
        mouseClientX_ = e.x;
        mouseX_ = jsRound(clampd(480 * ((e.x / e.canvasWidth) - 0.5), -240, 240));
    }
    if (e.hasY && e.y != 0) {
        mouseClientY_ = e.y;
        mouseY_ = jsRound(clampd(-360 * ((e.y / e.canvasHeight) - 0.5), -180, 180));
    }
    if (e.hasDown) {
        const bool previous = mouseDown_;
        mouseDown_ = e.isDown;
        if (previous == mouseDown_) return;
        if (e.wasDragged) return;
        if (!(e.x > 0 && e.x < e.canvasWidth && e.y > 0 && e.y < e.canvasHeight)) return;
        Target* target = stageTarget();
        if (host_ != nullptr) {
            const int id = host_->pick(e.x, e.y);
            for (const auto& t : project_.targets()) {
                if (t->drawableId == id && id >= 0) {
                    target = t.get();
                    break;
                }
            }
        }
        const bool isNewDown = !previous && mouseDown_;
        const bool isNewUp = previous && !mouseDown_;
        if ((target->draggable && isNewUp) || (!target->draggable && isNewDown)) {
            startHats("event_whenthisspriteclicked", nullptr, target);
            startHats("event_whenstageclicked", nullptr, target);
        }
    }
}

// --------------------------------------------------------------------- promises

void Runtime::waitForPromise(Thread& thread, PromiseKind kind, double dueMs, int requestId,
                             std::function<void()> onResolve) {
    if (thread.status == Thread::Status::Running) thread.status = Thread::Status::PromiseWait;
    promises_.push_back({thread.shared_from_this(), kind, dueMs, requestId, std::move(onResolve)});
}

void Runtime::completePromise(const std::shared_ptr<Thread>& threadPtr) {
    Thread& thread = *threadPtr;
    thread.status = Thread::Status::Running;  // handleReport
    // The promise belonged to a command block: continue after it.
    BlockIndex nextBlock;
    StackFrame* frame;
    do {
        if (thread.stack.empty()) return;
        const BlockIndex popped = thread.popStack();
        if (popped == kNoBlock) return;
        nextBlock = thread.target->blocks.getNextBlock(popped);
        if (nextBlock != kNoBlock) break;
        frame = thread.peekStackFrame();
    } while (frame != nullptr && !frame->isLoop);
    thread.pushStack(nextBlock);
}

void Runtime::resolveRequest(int requestId) {
    for (std::size_t i = 0; i < promises_.size(); i++) {
        if (promises_[i].kind == PromiseKind::External && promises_[i].requestId == requestId) {
            const std::shared_ptr<Thread> t = promises_[i].thread;
            promises_.erase(promises_.begin() + static_cast<std::ptrdiff_t>(i));
            completePromise(t);
            return;
        }
    }
}

void Runtime::flushPromises() {
    // Immediate promises resolve in creation order; timers once due.
    const double nowMs = clock_();
    std::vector<PendingPromise> pending;
    pending.swap(promises_);
    for (PendingPromise& p : pending) {
        const bool ready = p.kind == PromiseKind::Immediate || (p.kind == PromiseKind::Timer && nowMs >= p.dueMs);
        if (ready) {
            if (p.onResolve) p.onResolve();
            completePromise(p.thread);
        } else {
            promises_.push_back(std::move(p));
        }
    }
}

// --------------------------------------------------------------- ask and sound

void Runtime::askNextQuestion() {
    if (questions_.empty()) return;
    const Question& q = questions_.front();
    Target* target = targetByUid(q.targetUid);
    if (q.wasVisible && !q.wasStage && target != nullptr) {
        setBubble(*target, "say", Value::string(q.text));
        if (host_ != nullptr) host_->ask("");
    } else if (host_ != nullptr) {
        host_->ask(q.text.c_str());
    }
}

void Runtime::askAndWait(Thread& thread, const std::string& question) {
    const bool asked = !questions_.empty();
    Question q;
    q.text = question;
    q.thread = thread.shared_from_this();
    q.targetUid = thread.target->uid;
    q.wasVisible = thread.target->visible;
    q.wasStage = thread.target->isStage;
    questions_.push_back(std::move(q));
    waitForPromise(thread, PromiseKind::External, 0, -1);  // resolved by postAnswer
    if (!asked) askNextQuestion();
}

void Runtime::postAnswer(const std::string& answer) {
    answer_ = answer;
    if (questions_.empty()) return;
    Question q = std::move(questions_.front());
    questions_.erase(questions_.begin());
    if (q.wasVisible && !q.wasStage) {
        if (Target* t = targetByUid(q.targetUid)) setBubble(*t, "say", Value::string(""));
    }
    // resolve the waiting thread
    for (std::size_t i = 0; i < promises_.size(); i++) {
        if (promises_[i].kind == PromiseKind::External && promises_[i].requestId == -1 && promises_[i].thread == q.thread) {
            promises_.erase(promises_.begin() + static_cast<std::ptrdiff_t>(i));
            completePromise(q.thread);
            break;
        }
    }
    askNextQuestion();
}

void Runtime::clearTargetQuestions(Target& t) {
    const bool currentlyAsking = !questions_.empty() && questions_.front().targetUid == t.uid;
    questions_.erase(std::remove_if(questions_.begin(), questions_.end(), [&](const Question& q) { return q.targetUid == t.uid; }),
                     questions_.end());
    if (currentlyAsking) {
        setBubble(t, "say", Value::string(""));
        if (!questions_.empty()) {
            askNextQuestion();
        } else if (host_ != nullptr) {
            host_->ask(nullptr);
        }
    }
}

void Runtime::playSound(Thread* waitingThread, Target& t, int soundIndex) {
    // Without an audio engine a sprite has no sound bank: nothing plays and nothing is waited for.
    if (host_ == nullptr) return;
    if (waitingThread != nullptr) {
        auto& waiting = waitingSounds_[t.uid];
        if (std::find(waiting.begin(), waiting.end(), soundIndex) == waiting.end()) waiting.push_back(soundIndex);
        const int id = newRequestId();
        waitForPromise(*waitingThread, PromiseKind::External, 0, id);
        host_->playSound(t.uid, soundIndex, id);
    } else {
        auto& waiting = waitingSounds_[t.uid];
        waiting.erase(std::remove(waiting.begin(), waiting.end(), soundIndex), waiting.end());
        host_->playSound(t.uid, soundIndex, 0);
    }
}

void Runtime::stopAllSounds() {
    for (const auto& t : project_.targets()) {
        if (host_ != nullptr) host_->stopAllSounds(t->uid);
        waitingSounds_.erase(t->uid);
    }
}

}  // namespace lithium

// Lithium runtime: public C++ interface.
//
// The frontend never touches runtime internals; it goes through this class
// (or the flat C API in c_api.h, which wraps it for WASM/Node bindings).
//
// Execution model (ported from scratch-vm): the host calls step() once per
// frame (every 1000/60 ms, or 1000/30 ms in compatibility mode). A step runs
// every thread repeatedly until the work budget (75% of the frame time) is
// spent, a block requested a redraw (unless turbo mode is on), or no thread
// is still running.
#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "lithium/host.h"
#include "lithium/project.h"
#include "lithium/storage.h"
#include "lithium/thread.h"
#include "lithium/value.h"

namespace lithium {

class Sequencer;

class Runtime {
public:
    using Clock = std::function<double()>;  // milliseconds, monotonic
    using ThreadList = std::vector<std::shared_ptr<Thread>>;

    Runtime();
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    static std::string version();

    // ---- project -------------------------------------------------------
    Project& project() { return project_; }
    // Stops everything and replaces the project.
    void loadProject(Project project);

    // ---- configuration -------------------------------------------------
    // Replaces the time source (tests and the WASM host use this).
    void setClock(Clock nowMs);
    void setTurboMode(bool on) { turboMode_ = on; }
    // 30 steps/second instead of 60, like Scratch's compatibility mode.
    void setCompatibilityMode(bool on);
    // Whether a renderer is attached. Motion blocks only request a redraw
    // (ending the frame's work early) when one is, exactly as in scratch-vm.
    void setRendererAttached(bool on) { rendererAttached_ = on; }
    // Seeds the generator behind `pick random` (Math.random in scratch-vm).
    void setRandomSeed(std::uint32_t seed) { rng_.seed(seed); }
    bool turboMode() const { return turboMode_; }
    bool rendererAttached() const { return rendererAttached_; }

    // ---- host ----------------------------------------------------------
    // The platform (renderer, audio, ...). Not owned. Without a host the
    // runtime behaves like scratch-vm without a renderer.
    void setHost(Host* host);
    Host* host() const { return host_; }
    // (Re)creates the drawable of every target and pushes its current state
    // to the host. Call after the host has loaded costumes (skin ids set).
    void syncRenderer();

    // ---- execution -----------------------------------------------------
    void greenFlag();
    void stopAll();
    void stopForTarget(Target* target, Thread* except);
    void step();

    // ---- introspection -------------------------------------------------
    std::uint64_t frameCount() const { return frames_; }
    std::size_t threadCount() const { return threads_->size(); }
    double currentStepTimeMs() const { return currentStepTimeMs_; }
    double currentMSecs() const { return currentMSecs_; }
    bool redrawRequested() const { return redrawRequested_; }
    // Looks a variable up the way a block on `target` would (local, then
    // stage). std::nullopt if the target or variable does not exist.
    std::optional<Value> variableValue(std::string_view target, std::string_view variable);

    // ---- internal: used by the sequencer and block primitives ----------
    double now() const { return clock_(); }
    void updateCurrentMSecs() { currentMSecs_ = clock_(); }
    void requestRedraw() { redrawRequested_ = true; }
    // Uniform in [0, 1), like Math.random().
    double random() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng_); }
    Sequencer& sequencer() { return *sequencer_; }
    // The live thread list. stopAll() swaps in a new list; code that is
    // iterating keeps its own shared_ptr to the old one, like scratch-vm.
    std::shared_ptr<ThreadList> threads() const { return threads_; }
    void stopThread(Thread& thread);

    // Starts every script whose hat has this opcode (and, when given, whose
    // hat field `matchField.first` equals `matchField.second` ignoring case).
    // Returns the started threads. Runtime.startHats in scratch-vm.
    using HatField = std::pair<std::string, std::string>;
    // `onlyTarget` restricts the search to one target (clone start hats).
    std::vector<std::shared_ptr<Thread>> startHats(std::string_view opcode, const HatField* matchField = nullptr,
                                                   Target* onlyTarget = nullptr);

    // ---- sprite state (RenderedTarget) -------------------------------------
    void setXY(Target& t, double x, double y, bool force = false);
    void setDirection(Target& t, double direction);
    void setVisible(Target& t, bool visible);
    void setSize(Target& t, double size);
    void setEffect(Target& t, std::string_view name, double value);
    void clearEffects(Target& t);
    void setCostume(Target& t, double index);
    void setRotationStyle(Target& t, std::string_view style);
    void goToFront(Target& t);
    void goToBack(Target& t);
    void goForwardLayers(Target& t, double n);
    void goBackwardLayers(Target& t, double n);
    void goBehindOther(Target& t, Target& other);
    bool getBounds(Target& t, Rect* out);
    // keepInFence with the default stage fence; false if there are no bounds.
    bool keepInFence(Target& t, double newX, double newY, double* outX, double* outY);
    bool isTouchingObject(Target& t, const Value& what);
    bool isTouchingSprite(Target& t, const std::string& spriteName);
    Target* spriteByName(std::string_view name);  // first original sprite with that name
    Target* stageTarget();
    void renderedDirectionAndScale(const Target& t, double* direction, double* sx, double* sy) const;
    void updateAllDrawableProperties(Target& t);

    // ---- looks / sound state -------------------------------------------------
    void setBubble(Target& t, const std::string& type, const Value& message);
    void clearBubble(Target& t);
    void syncSoundState(Target& t);

    // ---- input devices ---------------------------------------------------------
    // Keyboard: `key` is a DOM key string (" ", "ArrowLeft", "a"...).
    void postKey(const std::string& key, bool isDown);
    bool keyIsDown(const Value& keyArg) const;
    // Mouse: coordinates in canvas pixels; canvas size for scaling.
    struct MouseEvent {
        bool hasX = false, hasY = false, hasDown = false;
        double x = 0, y = 0;
        double canvasWidth = 480, canvasHeight = 360;
        bool isDown = false;
        bool wasDragged = false;
    };
    void postMouse(const MouseEvent& e);
    double mouseX() const { return mouseX_; }
    double mouseY() const { return mouseY_; }
    bool mouseDown() const { return mouseDown_; }
    // Clock.projectTimer in seconds.
    double projectTimer() const { return (currentMSecs_ - timerStart_) / 1000.0; }
    void resetProjectTimer() { timerStart_ = currentMSecs_; }

    // ---- promises (primitives that finish later) -------------------------------
    enum class PromiseKind { Immediate, Timer, External };
    // The thread waits; Immediate resolves at the start of the next step (a JS
    // microtask), Timer after `dueMs` of wall-clock time, External when
    // resolveRequest(requestId) is called.
    void waitForPromise(Thread& thread, PromiseKind kind, double dueMs = 0.0, int requestId = 0,
                        std::function<void()> onResolve = nullptr);
    void resolveRequest(int requestId);
    int newRequestId() { return ++lastRequestId_; }
    // Runs after the current step so promise callbacks are not racing it.
    void flushPromises();

    // ---- persistent storage (OutList) ---------------------------------------------
    using StorageDone = std::function<void(StorageStatus, std::string&)>;
    // The platform's backend; null selects an in-memory one. Not owned.
    void setStorage(StorageBackend* backend);
    StorageBackend* storage();
    // Names saved lists are filed under; normally the project title.
    void setStorageScope(std::string scope);
    // Existence is answered from an index of saved names (a reporter cannot wait). Backends that
    // can list synchronously fill it themselves; others call primeStorageIndex.
    std::string storageAreaName() const { return storageArea(); }
    bool storageHas(const std::string& name);
    void primeStorageIndex(const std::vector<std::string>& names);
    void storageNoteSaved(const std::string& name) { storageIndex_.insert(name); }
    void storageNoteRemoved(const std::string& name) { storageIndex_.erase(name); }
    // The thread waits until the backend answers; `done` then runs at the start of a step.
    void storageRequest(Thread& thread, StorageOp op, const std::string& name, std::string data, StorageDone done);
    // Called by the backend (any time between steps); delivery is deferred to the next step.
    void storageComplete(int requestId, StorageStatus status, std::string data);
    struct ListBinding { std::string name; std::uint64_t fingerprint; bool unloaded = false; };
    std::unordered_map<const void*, ListBinding> listBindings;  // list -> file it matches
    std::string outListError;  // result of the last OutList block ("" = success)

    // ---- ask and wait -----------------------------------------------------------
    void askAndWait(Thread& thread, const std::string& question);
    void postAnswer(const std::string& answer);
    const std::string& answer() const { return answer_; }
    // ---- sound -----------------------------------------------------------------
    void playSound(Thread* waitingThread, Target& t, int soundIndex);
    void stopAllSounds();
    void soundFinished(int requestId) { resolveRequest(requestId); }

    double counter = 0;  // control_get_counter and friends

    // ---- clones ----------------------------------------------------------
    static constexpr int kMaxClones = 300;
    bool clonesAvailable() const { return cloneCounter_ < kMaxClones; }
    // Target.makeClone + runtime.addTarget + goBehindOther. nullptr when the
    // limit is hit or `original` is the stage.
    Target* createClone(Target& original);
    // runtime.disposeTarget + stopForTarget. The target stays allocated until
    // the next step so running threads never see a dangling pointer.
    void disposeClone(Target& clone);
    int cloneCount() const { return cloneCounter_; }
    // Runtime.isActiveThread / isWaitingThread.
    static bool isActiveThread(const Thread& thread);
    static bool isWaitingThread(const Thread& thread);

private:
    std::shared_ptr<Thread> pushThread(BlockIndex topBlock, Target* target);

    Project project_;
    std::shared_ptr<ThreadList> threads_;
    std::unique_ptr<Sequencer> sequencer_;
    Clock clock_;
    double currentMSecs_ = 0.0;
    double currentStepTimeMs_ = 1000.0 / 60.0;
    bool redrawRequested_ = false;
    bool turboMode_ = false;
    bool rendererAttached_ = false;
    std::uint64_t frames_ = 0;
    int cloneCounter_ = 0;
    Host* host_ = nullptr;
    int nextUid_ = 1;
    // input
    std::vector<std::string> keysPressed_;
    double mouseX_ = 0, mouseY_ = 0;
    double mouseClientX_ = std::numeric_limits<double>::quiet_NaN(), mouseClientY_ = std::numeric_limits<double>::quiet_NaN();
    bool mouseDown_ = false;
    double timerStart_ = 0;
    // promises
    struct PendingPromise {
        std::shared_ptr<Thread> thread;
        PromiseKind kind;
        double dueMs;
        int requestId;
        std::function<void()> onResolve;
    };
    std::vector<PendingPromise> promises_;
    StorageBackend* storage_ = nullptr;
    std::unique_ptr<StorageBackend> ownedStorage_;
    std::string storageScope_ = "project";
    struct StorageResult { int id; StorageStatus status; std::string data; };
    std::vector<StorageResult> storageDone_;
    std::unordered_map<int, StorageDone> storageWaiters_;
    void deliverStorage();
    std::string storageArea() const;
    std::unordered_set<std::string> storageIndex_;
    bool storageIndexReady_ = false;
    int lastRequestId_ = 0;
    void completePromise(const std::shared_ptr<Thread>& thread);
    // questions
    struct Question {
        std::string text;
        std::shared_ptr<Thread> thread;
        int targetUid;
        bool wasVisible, wasStage;
    };
    std::vector<Question> questions_;
    std::string answer_;
    void askNextQuestion();
    void clearTargetQuestions(Target& t);
    // sounds that "play until done" is waiting for: target uid -> sound indexes
    std::unordered_map<int, std::vector<int>> waitingSounds_;
    std::uint64_t nextUsageId_ = 1;
    Target* targetByUid(int uid);
    void initDrawable(Target& t);
    std::uint64_t nextCloneSerial_ = 1;
    std::vector<std::unique_ptr<Target>> graveyard_;
    std::mt19937_64 rng_{std::random_device{}()};
};

}  // namespace lithium

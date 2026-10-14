#include "lithium/c_api.h"

#include <string>
#include <vector>

#include "lithium/builtin_projects.h"
#include "lithium/inspect.h"
#include "lithium/sb3.h"
#include "lithium/text.h"
#include "lithium/value.h"
#include "lithium/runtime.h"

namespace {

// Forwards every Host call to the platform's single callback (see c_api.h).
class CallbackHost : public lithium::Host {
public:
    lithium_host_callback cb = nullptr;
    lithium::Runtime* runtime = nullptr;
    double call(int op, double a = 0, double b = 0, double c = 0, double d = 0, const void* p = nullptr) {
        return cb == nullptr ? 0.0 : cb(op, a, b, c, d, p);
    }
    int createDrawable(int layer) override { return static_cast<int>(call(1, layer)); }
    void destroyDrawable(int id, int) override { call(2, id); }
    void updateDrawableSkinId(int id, int skin) override { call(3, id, skin); }
    void updateDrawablePosition(int id, double x, double y) override { call(4, id, x, y); }
    void updateDrawableDirectionScale(int id, double dir, double sx, double sy) override { call(5, id, dir, sx, sy); }
    void updateDrawableVisible(int id, bool v) override { call(6, id, v ? 1 : 0); }
    void updateDrawableEffect(int id, const char* effect, double value) override { call(7, id, value, 0, 0, effect); }
    double setDrawableOrder(int id, double order, int layer, bool relative, double min) override {
        return call(8, id, order, layer + (relative ? 16 : 0), min);
    }
    void getFencedPosition(int id, double x, double y, double* ox, double* oy) override {
        double out[2] = {x, y};
        call(9, id, x, y, 0, out);
        *ox = out[0];
        *oy = out[1];
    }
    void getCurrentSkinSize(int id, double* w, double* h) override {
        double out[2] = {0, 0};
        call(10, id, 0, 0, 0, out);
        *w = out[0];
        *h = out[1];
    }
    bool getBounds(int id, lithium::Rect* r) override {
        double out[4] = {0, 0, 0, 0};
        if (call(11, id, 0, 0, 0, out) == 0) return false;
        r->left = out[0];
        r->right = out[1];
        r->top = out[2];
        r->bottom = out[3];
        return true;
    }
    bool drawableTouching(int id, double x, double y) override { return call(12, id, x, y) != 0; }
    bool isTouchingDrawables(int id, const int* others, int count) override {
        std::vector<int32_t> ids(others, others + count);
        return call(13, id, count, 0, 0, ids.data()) != 0;
    }
    bool isTouchingColor(int id, const int rgb[3]) override {
        int32_t v[3] = {rgb[0], rgb[1], rgb[2]};
        return call(14, id, 0, 0, 0, v) != 0;
    }
    bool colorIsTouchingColor(int id, const int target[3], const int mask[3]) override {
        int32_t v[6] = {target[0], target[1], target[2], mask[0], mask[1], mask[2]};
        return call(15, id, 0, 0, 0, v) != 0;
    }
    int pick(double x, double y) override { return static_cast<int>(call(16, x, y)); }
    void penClear() override { call(17); }
    void penStamp(int id) override { call(18, id); }
    void penPoint(const double c[4], double diameter, double x, double y) override {
        const double v[7] = {c[0], c[1], c[2], c[3], diameter, x, y};
        call(19, 0, 0, 0, 0, v);
    }
    void penLine(const double c[4], double diameter, double x0, double y0, double x1, double y1) override {
        const double v[9] = {c[0], c[1], c[2], c[3], diameter, x0, y0, x1, y1};
        call(20, 0, 0, 0, 0, v);
    }
    void setBubble(int uid, int drawable, const char* type, const char* text) override {
        call(21, uid, drawable, type[0] == 't' ? 1 : 0, 0, text);
    }
    void ask(const char* question) override { call(22, question != nullptr ? 1 : 0, 0, 0, 0, question); }
    // d = index of the sprite (among originals) whose sounds these are; clones share their original's.
    void playSound(int uid, int index, int request) override {
        int owner = -1, n = 0;
        const lithium::Target* me = nullptr;
        for (const auto& t : runtime->project().targets()) if (t->uid == uid) me = t.get();
        for (const auto& t : runtime->project().targets()) {
            if (!t->isOriginal) continue;
            if (me != nullptr && t->assets == me->assets) { owner = n; break; }
            n++;
        }
        call(23, uid, index, request, owner);
    }
    void stopSound(int uid, int index) override { call(24, uid, index); }
    void stopAllSounds(int uid) override { call(25, uid); }
    void setSoundEffects(int uid, double volume, double pitch, double pan) override { call(26, uid, volume, pitch, pan); }
    void targetRemoved(int uid) override { call(27, uid); }
};

}  // namespace

struct lithium_runtime {
    CallbackHost host;
    lithium::Runtime impl;
    double manualNow = 0.0;
    double autoAdvance = 0.0;
    std::string stringBuffer;
    std::string loadLog;
    std::string stateBuffer;
};

extern "C" {

lithium_runtime* lithium_runtime_create(void) { return new lithium_runtime(); }

void lithium_runtime_destroy(lithium_runtime* rt) { delete rt; }

const char* lithium_version(void) {
    static const std::string v = lithium::Runtime::version();
    return v.c_str();
}

int32_t lithium_load_builtin(lithium_runtime* rt, const char* name) {
    auto project = lithium::builtinProject(name ? name : "");
    if (!project) return 0;
    rt->impl.loadProject(std::move(*project));
    return 1;
}

int32_t lithium_load_sb3_json(lithium_runtime* rt, const char* project_json) {
    auto result = lithium::loadSb3Json(project_json ? project_json : "");
    rt->loadLog.clear();
    if (!result.project) {
        rt->loadLog = "error: " + result.error + "\n";
        return 0;
    }
    for (const auto& w : result.warnings) rt->loadLog += "warning: " + w + "\n";
    rt->impl.loadProject(std::move(*result.project));
    return 1;
}

const char* lithium_get_load_log(lithium_runtime* rt) { return rt->loadLog.c_str(); }

const char* lithium_dump_state(lithium_runtime* rt) {
    rt->stateBuffer = lithium::dumpState(rt->impl);
    return rt->stateBuffer.c_str();
}

void lithium_set_renderer_attached(lithium_runtime* rt, int32_t on) { rt->impl.setRendererAttached(on != 0); }
void lithium_set_random_seed(lithium_runtime* rt, uint32_t seed) { rt->impl.setRandomSeed(seed); }
void lithium_set_turbo_mode(lithium_runtime* rt, int32_t on) { rt->impl.setTurboMode(on != 0); }
void lithium_set_compatibility_mode(lithium_runtime* rt, int32_t on) { rt->impl.setCompatibilityMode(on != 0); }

void lithium_use_manual_clock(lithium_runtime* rt, double auto_advance_ms) {
    rt->manualNow = 0.0;
    rt->autoAdvance = auto_advance_ms;
    rt->impl.setClock([rt]() {
        const double t = rt->manualNow;
        rt->manualNow += rt->autoAdvance;
        return t;
    });
}

void lithium_advance_clock(lithium_runtime* rt, double ms) { rt->manualNow += ms; }

void lithium_green_flag(lithium_runtime* rt) { rt->impl.greenFlag(); }
void lithium_stop_all(lithium_runtime* rt) { rt->impl.stopAll(); }
void lithium_runtime_step(lithium_runtime* rt) { rt->impl.step(); }

double lithium_runtime_frame_count(const lithium_runtime* rt) {
    return static_cast<double>(rt->impl.frameCount());
}

int32_t lithium_thread_count(const lithium_runtime* rt) {
    return static_cast<int32_t>(rt->impl.threadCount());
}

int32_t lithium_get_variable_number(lithium_runtime* rt, const char* target, const char* name, double* out) {
    auto v = rt->impl.variableValue(target, name);
    if (!v) return 0;
    *out = v->toNumber();
    return 1;
}

const char* lithium_get_variable_string(lithium_runtime* rt, const char* target, const char* name) {
    auto v = rt->impl.variableValue(target, name);
    if (!v) return nullptr;
    rt->stringBuffer = v->toString();
    return rt->stringBuffer.c_str();
}

int32_t lithium_get_position(lithium_runtime* rt, const char* target, double* x, double* y) {
    lithium::Target* t = rt->impl.project().findTarget(target);
    if (!t) return 0;
    *x = t->x;
    *y = t->y;
    return 1;
}

}  // extern "C"

extern "C" {

void lithium_set_host_callback(lithium_runtime* rt, lithium_host_callback cb) {
    rt->host.cb = cb;
    rt->host.runtime = &rt->impl;
    rt->impl.setHost(cb != nullptr ? &rt->host : nullptr);
}

const char* lithium_asset_manifest(lithium_runtime* rt) {
    using lithium::jsonQuote;
    std::string out = "[";
    bool first = true;
    for (const auto& t : rt->impl.project().targets()) {
        if (!t->isOriginal) continue;
        if (!first) out += ',';
        first = false;
        out += "{\"name\":" + jsonQuote(t->isStage ? "Stage" : t->name) + ",\"costumes\":[";
        const auto& assets = *t->assets;
        for (std::size_t i = 0; i < assets.costumes.size(); i++) {
            const auto& c = assets.costumes[i];
            out += std::string(i ? "," : "") + "{\"name\":" + jsonQuote(c.name) + ",\"md5ext\":" + jsonQuote(c.md5ext) +
                   ",\"dataFormat\":" + jsonQuote(c.dataFormat) + ",\"rotationCenterX\":" + lithium::numberToJsString(c.rotationCenterX) +
                   ",\"rotationCenterY\":" + lithium::numberToJsString(c.rotationCenterY) +
                   ",\"bitmapResolution\":" + lithium::numberToJsString(c.bitmapResolution) + "}";
        }
        out += "],\"sounds\":[";
        for (std::size_t i = 0; i < assets.sounds.size(); i++) {
            const auto& s = assets.sounds[i];
            out += std::string(i ? "," : "") + "{\"name\":" + jsonQuote(s.name) + ",\"md5ext\":" + jsonQuote(s.md5ext) +
                   ",\"dataFormat\":" + jsonQuote(s.dataFormat) + ",\"rate\":" + lithium::numberToJsString(s.rate) +
                   ",\"sampleCount\":" + lithium::numberToJsString(s.sampleCount) + "}";
        }
        out += "]}";
    }
    out += "]";
    rt->stringBuffer = out;
    return rt->stringBuffer.c_str();
}

void lithium_set_costume_skin(lithium_runtime* rt, int32_t target_index, int32_t costume_index, int32_t skin_id) {
    int n = 0;
    for (const auto& t : rt->impl.project().targets()) {
        if (!t->isOriginal) continue;
        if (n++ != target_index) continue;
        auto& costumes = t->assets->costumes;
        if (costume_index >= 0 && static_cast<std::size_t>(costume_index) < costumes.size()) {
            costumes[static_cast<std::size_t>(costume_index)].skinId = skin_id;
        }
        return;
    }
}

void lithium_sync_renderer(lithium_runtime* rt) { rt->impl.syncRenderer(); }

void lithium_post_key(lithium_runtime* rt, const char* key, int32_t is_down) { rt->impl.postKey(key ? key : "", is_down != 0); }

/* flags: 1 = has x, 2 = has y, 4 = has down, 8 = is down, 16 = was dragged */
void lithium_post_mouse(lithium_runtime* rt, int32_t flags, double x, double y, double canvas_w, double canvas_h) {
    lithium::Runtime::MouseEvent e;
    e.hasX = flags & 1;
    e.hasY = flags & 2;
    e.hasDown = flags & 4;
    e.isDown = flags & 8;
    e.wasDragged = flags & 16;
    e.x = x;
    e.y = y;
    e.canvasWidth = canvas_w;
    e.canvasHeight = canvas_h;
    rt->impl.postMouse(e);
}

void lithium_post_answer(lithium_runtime* rt, const char* answer) { rt->impl.postAnswer(answer ? answer : ""); }
void lithium_sound_finished(lithium_runtime* rt, int32_t request_id) { rt->impl.soundFinished(request_id); }

int32_t lithium_take_redraw(lithium_runtime* rt) {
    const bool r = rt->impl.redrawRequested();
    return r ? 1 : 0;
}

}  // extern "C"

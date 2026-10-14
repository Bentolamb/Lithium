#include "lithium/c_api.h"

#include <string>

#include "lithium/builtin_projects.h"
#include "lithium/runtime.h"

struct lithium_runtime {
    lithium::Runtime impl;
    double manualNow = 0.0;
    double autoAdvance = 0.0;
    std::string stringBuffer;
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

void lithium_set_renderer_attached(lithium_runtime* rt, int32_t on) { rt->impl.setRendererAttached(on != 0); }
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

#include "lithium/c_api.h"

#include "lithium/runtime.h"

struct lithium_runtime {
    lithium::Runtime impl;
};

extern "C" {

lithium_runtime* lithium_runtime_create(void) { return new lithium_runtime(); }

void lithium_runtime_destroy(lithium_runtime* rt) { delete rt; }

const char* lithium_version(void) {
    static const std::string v = lithium::Runtime::version();
    return v.c_str();
}

void lithium_runtime_step(lithium_runtime* rt) { rt->impl.step(); }

uint64_t lithium_runtime_frame_count(const lithium_runtime* rt) {
    return rt->impl.frameCount();
}

}  // extern "C"

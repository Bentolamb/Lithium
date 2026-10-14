// Flat C API over the runtime. This is the only surface the JS side (WASM or
// a Node addon) binds to, so C++ internals can change freely.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lithium_runtime lithium_runtime;

lithium_runtime* lithium_runtime_create(void);
void lithium_runtime_destroy(lithium_runtime* rt);
const char* lithium_version(void);
void lithium_runtime_step(lithium_runtime* rt);
uint64_t lithium_runtime_frame_count(const lithium_runtime* rt);

#ifdef __cplusplus
}
#endif

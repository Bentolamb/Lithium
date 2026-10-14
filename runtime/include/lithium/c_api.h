// Flat C API over the runtime. This is the only surface the JS side (WASM or
// a Node addon) binds to, so C++ internals can change freely.
//
// Hot-path functions use int32/double only: uint64_t crosses the WASM
// boundary as BigInt.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lithium_runtime lithium_runtime;

lithium_runtime* lithium_runtime_create(void);
void lithium_runtime_destroy(lithium_runtime* rt);
const char* lithium_version(void);

/* Projects. Returns 1 on success, 0 if the name is unknown. */
int32_t lithium_load_builtin(lithium_runtime* rt, const char* name);

/* Load an SB3 project from the text of its project.json. Returns 1 on
 * success, 0 on failure. Either way lithium_get_load_log() describes what
 * happened (the error, or warnings about dropped features), one per line. */
int32_t lithium_load_sb3_json(lithium_runtime* rt, const char* project_json);
const char* lithium_get_load_log(lithium_runtime* rt);

/* Configuration. */
void lithium_set_renderer_attached(lithium_runtime* rt, int32_t on);
void lithium_set_turbo_mode(lithium_runtime* rt, int32_t on);
void lithium_set_compatibility_mode(lithium_runtime* rt, int32_t on);
/* Replace the wall clock with a manual one starting at 0 ms. Every clock
 * read then advances it by auto_advance_ms (0 = only advance_clock moves it). */
void lithium_use_manual_clock(lithium_runtime* rt, double auto_advance_ms);
void lithium_advance_clock(lithium_runtime* rt, double ms);

/* Execution. */
void lithium_green_flag(lithium_runtime* rt);
void lithium_stop_all(lithium_runtime* rt);
void lithium_runtime_step(lithium_runtime* rt);

/* Introspection. */
double lithium_runtime_frame_count(const lithium_runtime* rt);
int32_t lithium_thread_count(const lithium_runtime* rt);
/* Variable lookup from a target by name. Returns 1 if found. */
int32_t lithium_get_variable_number(lithium_runtime* rt, const char* target, const char* name, double* out);
/* Returned pointer is valid until the next call to this function; NULL if missing. */
const char* lithium_get_variable_string(lithium_runtime* rt, const char* target, const char* name);
/* JSON snapshot of frames, threads and every target's variables. Valid until
 * the next call to any string-returning function of this runtime. */
const char* lithium_dump_state(lithium_runtime* rt);
int32_t lithium_get_position(lithium_runtime* rt, const char* target, double* x, double* y);

#ifdef __cplusplus
}
#endif

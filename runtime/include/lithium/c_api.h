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
void lithium_set_random_seed(lithium_runtime* rt, uint32_t seed);
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

/* ---- Host (renderer / audio / input) ------------------------------------
 * The platform implements one callback; the runtime calls it for every
 * drawing, geometry, pen, bubble and sound request. Signature:
 *   double cb(int op, double a, double b, double c, double d, const void* p)
 * Ops (a,b,c,d are numbers, p is a pointer into WASM memory when noted):
 *    1 create drawable (a=layer group) -> id        2 destroy (a=id)
 *    3 skin (a=id, b=skinId)                        4 position (a=id, b=x, c=y)
 *    5 direction/scale (a=id, b=dir, c=sx, d=sy)    6 visible (a=id, b=0/1)
 *    7 effect (a=id, b=value, p=char* name)
 *    8 order (a=id, b=order, c=layerGroup + 16*relative, d=min) -> new order
 *    9 fenced position (a=id, b=x, c=y; writes double[2] at p)
 *   10 skin size (a=id; writes double[2] at p)
 *   11 bounds (a=id; writes double[4] left,right,top,bottom at p) -> 0/1
 *   12 point touches drawable (a=id, b=x, c=y) -> 0/1
 *   13 touching drawables (a=id, b=count, p=int32[count]) -> 0/1
 *   14 touching color (a=id, p=int32[3]) -> 0/1
 *   15 color touching color (a=id, p=int32[6] = target rgb, mask rgb) -> 0/1
 *   16 pick (a=canvas x, b=canvas y) -> drawable id or -1
 *   17 pen clear       18 pen stamp (a=id)
 *   19 pen point (p=double[7]: rgba, diameter, x, y)
 *   20 pen line  (p=double[9]: rgba, diameter, x0, y0, x1, y1)
 *   21 bubble (a=target uid, b=drawable id, c=1 if "think", p=char* text)
 *   22 ask (a=1 show / 0 hide, p=char* question)
 *   23 play sound (a=target uid, b=sound index, c=request id (0 = fire and forget),
 *      d=index of the sprite among the originals that owns the sounds)
 *   24 stop sound (a=uid, b=index)    25 stop all sounds (a=uid, -1 = every target)
 *   26 sound effects (a=uid, b=volume, c=pitch, d=pan)
 *   27 target removed (a=uid)
 */
typedef double (*lithium_host_callback)(int32_t op, double a, double b, double c, double d, const void* p);
void lithium_set_host_callback(lithium_runtime* rt, lithium_host_callback cb);
/* ---- OutList storage -------------------------------------------------------
 * The platform answers storage requests asynchronously. The callback receives
 * (request id, op, project area, list name, data pointer, data length) with
 * op 0 write, 1 read, 2 exists, 3 delete; pointers are valid during the call only.
 * It must later call lithium_storage_complete(id, status, data, len) with status
 * 0 ok, 1 not found, 2 error (data = file contents for a successful read).
 * Write must be atomic or recoverable. Without a callback an in-memory store is used. */
typedef void (*lithium_storage_callback)(int32_t request_id, int32_t op, const char* area, const char* name,
                                         const uint8_t* data, int32_t length);
void lithium_set_storage_callback(lithium_runtime* rt, lithium_storage_callback cb);
void lithium_storage_complete(lithium_runtime* rt, int32_t request_id, int32_t status, const uint8_t* data, int32_t length);
/* Native builds: keep saved lists as files under `directory`. */
void lithium_set_storage_directory(lithium_runtime* rt, const char* directory);
/* Name that scopes saved lists (the project title). */
void lithium_set_storage_scope(lithium_runtime* rt, const char* scope);
/* Names already saved for the current scope, separated by '\n' (answers "exists?"). */
void lithium_storage_prime(lithium_runtime* rt, const char* names);
/* The area name the current scope maps to (the first argument the storage callback receives). */
const char* lithium_storage_area(lithium_runtime* rt);

/* JSON: [{"name":..., "costumes":[{name,md5ext,dataFormat,rotationCenterX,
 * rotationCenterY,bitmapResolution}], "sounds":[{name,md5ext,dataFormat,rate,
 * sampleCount}]}] for every original target, in project order. */
const char* lithium_asset_manifest(lithium_runtime* rt);
void lithium_set_costume_skin(lithium_runtime* rt, int32_t target_index, int32_t costume_index, int32_t skin_id);
void lithium_sync_renderer(lithium_runtime* rt);
/* Input. */
void lithium_post_key(lithium_runtime* rt, const char* key, int32_t is_down);
void lithium_post_mouse(lithium_runtime* rt, int32_t flags, double x, double y, double canvas_w, double canvas_h);
void lithium_post_answer(lithium_runtime* rt, const char* answer);
void lithium_sound_finished(lithium_runtime* rt, int32_t request_id);
/* 1 when a redraw is wanted (resets the flag). */
int32_t lithium_take_redraw(lithium_runtime* rt);

#ifdef __cplusplus
}
#endif

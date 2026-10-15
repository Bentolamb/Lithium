// Lithium-only extensions: Lithium Lab, Packin and OutList.
#include <algorithm>
#include <cctype>
#include <cmath>

#include "lithium/outlist.h"
#include "lithium/packin.h"
#include "lithium/runtime.h"
#include "primitives.h"

namespace lithium {

namespace {

using Kind = Primitive::Kind;

// ------------------------------------------------------------------ Lithium Lab
//
// Environment answers (Lithium's own runtime, so these are fixed):
//   is TurboWarp?  false  Lithium is not TurboWarp. True would need TurboWarp's own runtime.
//   is Lithium?    true
//   is Compiled?   false  true only when the project's scripts run as compiled code (TurboWarp's
//                         compiler, say). Lithium interprets scripts, even though the interpreter
//                         itself is compiled C++ or WebAssembly.
// The same answers are given by the boolean parameters "is TurboWarp?", "is Compiled?" and
// "is Lithium?" (see argumentReporterBoolean), so the usual Scratch/TurboWarp trick works.

Value labIsTurboWarp(PrimitiveContext&) { return Value::boolean(false); }
Value labIsCompiled(PrimitiveContext&) { return Value::boolean(false); }
Value labIsLithium(PrimitiveContext&) { return Value::boolean(true); }

// Capability test: true when the runtime has blocks for the extension. The name is matched
// ignoring case, spaces, hyphens and underscores ("Lithium Lab" = lithiumlab).
Value labIsExtensionAvailable(PrimitiveContext& cx) {
    std::string name;
    for (unsigned char c : cx.input("NAME").toString()) {
        if (c == ' ' || c == '-' || c == '_') continue;
        name.push_back(static_cast<char>(std::tolower(c)));
    }
    return Value::boolean(!name.empty() && extensionHasPrimitives(name));
}

// ----------------------------------------------------------------------- Packin
//
// Data is text: a Scratch string. Packing takes the string's UTF-8 bytes, so any text, including
// text holding binary data, comes back exactly. The packed result is a byte string: one
// character U+0000..U+00FF per byte, which survives lists, variables and file blocks.

std::string packedToBytes(const std::string& s) {
    const auto b = packin::fromByteString(s);
    return b ? *b : std::string();
}

int levelOf(const Value& v) {
    const double d = v.toNumber();
    if (!(d == d)) return packin::kDefaultLevel;
    return static_cast<int>(std::clamp(std::floor(d + 0.5), static_cast<double>(packin::kMinLevel), static_cast<double>(packin::kMaxLevel)));
}

Value packinCompress(PrimitiveContext& cx) {
    return Value::string(packin::toByteString(packin::compress(cx.input("DATA").toString())));
}

Value packinCompressAt(PrimitiveContext& cx) {
    return Value::string(packin::toByteString(packin::compress(cx.input("DATA").toString(), levelOf(cx.input("LEVEL")))));
}

// Empty text when the input is not valid, intact Packin data (check with "is valid Packin data?").
Value packinDecompress(PrimitiveContext& cx) {
    const auto out = packin::decompress(packedToBytes(cx.input("DATA").toString()));
    return Value::string(out ? *out : std::string());
}

// Length in bytes of the packed form of the (unpacked) input at the default level.
Value packinCompressedSize(PrimitiveContext& cx) {
    return Value::number(static_cast<double>(packin::compress(cx.input("DATA").toString()).size()));
}

// Original length in bytes (UTF-8) of packed data, from its validated header; 0 when invalid.
Value packinOriginalSize(PrimitiveContext& cx) {
    const auto h = packin::readHeader(packedToBytes(cx.input("DATA").toString()));
    return Value::number(h ? static_cast<double>(h->originalLength) : 0.0);
}

Value packinIsValid(PrimitiveContext& cx) {
    return Value::boolean(packin::readHeader(packedToBytes(cx.input("DATA").toString())).has_value());
}

// ---------------------------------------------------------------------- OutList

Variable& listOf(PrimitiveContext& cx) {
    const Field* f = cx.field("LIST");
    static Variable scratch;
    if (f == nullptr) { scratch = Variable{}; scratch.type = Variable::Type::List; return scratch; }
    return cx.target().lookupOrCreateList(f->id, f->value.toString());
}

// Fails the block without waiting.
void fail(PrimitiveContext& cx, const std::string& message) { cx.runtime.outListError = message; }

bool nameOrFail(PrimitiveContext& cx, std::string& name) {
    name = cx.input("NAME").toString();
    const std::string why = outlist::validateName(name);
    if (!why.empty()) { fail(cx, "invalid name: " + why); return false; }
    return true;
}

std::string describe(StorageStatus s, const std::string& what) {
    return s == StorageStatus::NotFound ? "no saved list named \"" + what + "\"" : "storage error while accessing \"" + what + "\"";
}

// Finds the list again when the storage answer arrives (the thread's target may be gone).
Variable* findList(Runtime& rt, int targetUid, const std::string& id, const std::string& name) {
    for (const auto& t : rt.project().targets()) {
        if (t->uid == targetUid) return &t->lookupOrCreateList(id, name);
    }
    return nullptr;
}

void outlistSave(PrimitiveContext& cx) {
    std::string name;
    if (!nameOrFail(cx, name)) return;
    Variable& list = listOf(cx);
    auto binding = cx.runtime.listBindings.find(&list);
    if (binding != cx.runtime.listBindings.end() && binding->second.unloaded && binding->second.name == name && list.list.empty()) {
        fail(cx, "list \"" + name + "\" was unloaded; load it before saving so the file is not overwritten with an empty list");
        return;
    }
    if (list.list.size() > outlist::kMaxItems) { fail(cx, "list is too long"); return; }
    const std::uint64_t fp = outlist::fingerprint(list.list);
    const void* key = &list;
    Runtime* rt = &cx.runtime;
    rt->outListError.clear();
    rt->storageRequest(cx.thread, StorageOp::Write, name, outlist::encode(list.list), [rt, key, name, fp](StorageStatus s, std::string&) {
        if (s == StorageStatus::Ok) {
            rt->outListError.clear();
            rt->storageNoteSaved(name);
            rt->listBindings[key] = {name, fp, false};
        } else {
            rt->outListError = "could not save \"" + name + "\": " + describe(s, name);
        }
    });
}

void outlistLoad(PrimitiveContext& cx) {
    std::string name;
    if (!nameOrFail(cx, name)) return;
    const Field* f = cx.field("LIST");
    if (f == nullptr) { fail(cx, "no list selected"); return; }
    const std::string id = f->id, listName = f->value.toString();
    const int uid = cx.target().uid;
    Runtime* rt = &cx.runtime;
    rt->outListError.clear();
    rt->storageRequest(cx.thread, StorageOp::Read, name, {}, [rt, uid, id, listName, name](StorageStatus s, std::string& data) {
        if (s != StorageStatus::Ok) {
            if (s == StorageStatus::NotFound) rt->storageNoteRemoved(name);
            rt->outListError = describe(s, name);
            return;
        }
        std::string why;
        auto items = outlist::decode(data, why);
        if (!items) { rt->outListError = "saved list \"" + name + "\" is damaged: " + why; return; }  // list left untouched
        Variable* list = findList(*rt, uid, id, listName);
        if (list == nullptr) { rt->outListError = "the sprite is gone"; return; }
        list->list = std::move(*items);
        rt->outListError.clear();
        rt->listBindings[list] = {name, outlist::fingerprint(list->list), false};
    });
}

Value outlistExists(PrimitiveContext& cx) {
    const std::string name = cx.input("NAME").toString();
    if (!outlist::validateName(name).empty()) return Value::boolean(false);
    return Value::boolean(cx.runtime.storageHas(name));
}

void outlistDelete(PrimitiveContext& cx) {
    std::string name;
    if (!nameOrFail(cx, name)) return;
    Runtime* rt = &cx.runtime;
    rt->outListError.clear();
    rt->storageRequest(cx.thread, StorageOp::Delete, name, {}, [rt, name](StorageStatus s, std::string&) {
        if (s == StorageStatus::Ok || s == StorageStatus::NotFound) {
            rt->storageNoteRemoved(name);
            for (auto it = rt->listBindings.begin(); it != rt->listBindings.end();) {
                it = it->second.name == name ? rt->listBindings.erase(it) : std::next(it);
            }
        }
        rt->outListError = s == StorageStatus::Ok ? std::string() : describe(s, name);
    });
}

// Releases a list's memory, but only when its saved file holds exactly what is in memory.
// A list that was never saved or loaded, or has changed since, is left alone and the error
// says why. After an unload the list is empty until loaded again.
void outlistUnload(PrimitiveContext& cx) {
    Variable& list = listOf(cx);
    auto binding = cx.runtime.listBindings.find(&list);
    if (binding == cx.runtime.listBindings.end()) {
        fail(cx, "list was not saved or loaded, so it was not unloaded");
        return;
    }
    if (binding->second.unloaded && list.list.empty()) { cx.runtime.outListError.clear(); return; }
    if (outlist::fingerprint(list.list) != binding->second.fingerprint) {
        fail(cx, "list has unsaved changes, so it was not unloaded");
        return;
    }
    std::vector<Value>().swap(list.list);
    binding->second.unloaded = true;
    cx.runtime.outListError.clear();
}

Value outlistLastError(PrimitiveContext& cx) { return Value::string(cx.runtime.outListError); }

const Primitive kLithium[] = {
    {"lithiumlab_isTurboWarp", Kind::Reporter, nullptr, &labIsTurboWarp, false},
    {"lithiumlab_isCompiled", Kind::Reporter, nullptr, &labIsCompiled, false},
    {"lithiumlab_isLithium", Kind::Reporter, nullptr, &labIsLithium, false},
    {"lithiumlab_isExtensionAvailable", Kind::Reporter, nullptr, &labIsExtensionAvailable, false},

    {"packin_compress", Kind::Reporter, nullptr, &packinCompress, false},
    {"packin_decompress", Kind::Reporter, nullptr, &packinDecompress, false},
    {"packin_compressedSize", Kind::Reporter, nullptr, &packinCompressedSize, false},
    {"packin_originalSize", Kind::Reporter, nullptr, &packinOriginalSize, false},
    {"packin_isValid", Kind::Reporter, nullptr, &packinIsValid, false},
    {"packin_compressAtLevel", Kind::Reporter, nullptr, &packinCompressAt, false},

    {"outlist_saveList", Kind::Command, &outlistSave, nullptr, false},
    {"outlist_loadList", Kind::Command, &outlistLoad, nullptr, false},
    {"outlist_exists", Kind::Reporter, nullptr, &outlistExists, false},
    {"outlist_deleteList", Kind::Command, &outlistDelete, nullptr, false},
    {"outlist_unloadList", Kind::Command, &outlistUnload, nullptr, false},
    {"outlist_lastError", Kind::Reporter, nullptr, &outlistLastError, false},
};

}  // namespace

void registerLithiumPrimitives(PrimitiveMap& map) {
    for (const Primitive& p : kLithium) map.emplace(p.opcode, &p);
}

}  // namespace lithium

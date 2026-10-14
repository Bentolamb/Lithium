// Read-only state snapshot, for tools and tests.
#pragma once

#include <string>

namespace lithium {

class Runtime;

// JSON: {"frames":N,"threads":N,"targets":[{"name","isStage","x","y",
// "direction","size","visible","variables":{name:value}}]}.
// Non-finite numbers are written as the strings "NaN", "Infinity", "-Infinity".
std::string dumpState(Runtime& runtime);

}  // namespace lithium

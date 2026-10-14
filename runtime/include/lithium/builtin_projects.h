// Built-in demo projects, used by tests and the WASM smoke test until the SB3
// loader exists.
#pragma once

#include <optional>
#include <string_view>

#include "lithium/project.h"

namespace lithium {

// Names: "repeat_counter", "slide", "wait", "forever_counter".
std::optional<Project> builtinProject(std::string_view name);

}  // namespace lithium

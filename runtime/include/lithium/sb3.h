// Scratch 3 project loader (project.json -> Project).
//
// Takes the text of project.json; unzipping the .sb3 is the host's job (the
// browser, Electron and the Node runner each have a zip reader already).
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lithium/project.h"

namespace lithium {

struct Sb3LoadResult {
    std::optional<Project> project;   // empty on failure
    std::string error;                // set on failure
    std::vector<std::string> warnings;  // loaded, but something was dropped
};

Sb3LoadResult loadSb3Json(std::string_view projectJson);

}  // namespace lithium

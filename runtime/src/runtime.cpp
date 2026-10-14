#include "lithium/runtime.h"

namespace lithium {

std::string Runtime::version() { return "0.0.1"; }

void Runtime::step() { ++frames_; }

}  // namespace lithium

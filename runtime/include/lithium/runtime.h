// Lithium runtime: public C++ interface.
// The frontend never touches runtime internals; it goes through this class
// (or the C API in c_api.h, which wraps it for WASM/Node bindings).
#pragma once

#include <cstdint>
#include <string>

namespace lithium {

class Runtime {
public:
    Runtime() = default;

    // Runtime version string, e.g. "0.0.1".
    static std::string version();

    // Number of frames stepped since creation. Placeholder state used to
    // verify the frontend/runtime boundary before the scheduler exists.
    std::uint64_t frameCount() const { return frames_; }

    // Advance the runtime by one frame.
    void step();

private:
    std::uint64_t frames_ = 0;
};

}  // namespace lithium

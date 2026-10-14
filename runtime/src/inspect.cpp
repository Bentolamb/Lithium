#include "lithium/inspect.h"

#include <cmath>

#include "lithium/runtime.h"
#include "nlohmann/json.hpp"

namespace lithium {

namespace {

using Json = nlohmann::ordered_json;

Json numberJson(double d) {
    if (std::isnan(d)) return "NaN";
    if (std::isinf(d)) return d > 0 ? "Infinity" : "-Infinity";
    return d;
}

Json valueJson(const Value& v) {
    if (v.isNumber()) return numberJson(v.toJsNumber());
    if (v.isBoolean()) return v.toBoolean();
    return v.toString();
}

}  // namespace

std::string dumpState(Runtime& rt) {
    Json out;
    out["frames"] = rt.frameCount();
    out["threads"] = rt.threadCount();
    Json targets = Json::array();
    for (const auto& t : rt.project().targets()) {
        Json j;
        j["name"] = t->name;
        j["isStage"] = t->isStage;
        j["x"] = numberJson(t->x);
        j["y"] = numberJson(t->y);
        j["direction"] = numberJson(t->direction);
        j["size"] = numberJson(t->size);
        j["visible"] = t->visible;
        Json vars = Json::object();
        for (const Variable& v : t->variables) vars[v.name] = valueJson(v.value);
        j["variables"] = vars;
        targets.push_back(std::move(j));
    }
    out["targets"] = std::move(targets);
    return out.dump();
}

}  // namespace lithium

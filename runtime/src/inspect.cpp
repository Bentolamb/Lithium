#include "lithium/inspect.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "lithium/runtime.h"
#include "lithium/text.h"

namespace lithium {

namespace {

std::string number(double d) {
    if (std::isnan(d)) return "\"NaN\"";
    if (std::isinf(d)) return d > 0 ? "\"Infinity\"" : "\"-Infinity\"";
    return numberToJsString(d);  // valid JSON: digits, optional fraction/exponent
}

std::string value(const Value& v) {
    if (v.isNumber()) return number(v.toJsNumber());
    if (v.isBoolean()) return v.toBoolean() ? "true" : "false";
    return jsonQuote(v.toString());
}

}  // namespace

std::string dumpState(Runtime& rt) {
    std::string out = "{\"frames\":" + std::to_string(rt.frameCount()) + ",\"threads\":" + std::to_string(rt.threadCount()) +
                      ",\"targets\":[";
    bool firstTarget = true;
    // Scratch's runtime.targets order: originals in layer order, then clones in creation order.
    std::vector<const Target*> order;
    for (const auto& t : rt.project().targets()) {
        if (t->isOriginal) order.push_back(t.get());
    }
    std::vector<const Target*> clones;
    for (const auto& t : rt.project().targets()) {
        if (!t->isOriginal) clones.push_back(t.get());
    }
    std::sort(clones.begin(), clones.end(), [](const Target* a, const Target* b) { return a->cloneSerial < b->cloneSerial; });
    order.insert(order.end(), clones.begin(), clones.end());
    for (const Target* tp : order) {
        const Target* t = tp;
        if (!firstTarget) out += ',';
        firstTarget = false;
        out += "{\"name\":" + jsonQuote(t->name) + ",\"isStage\":" + (t->isStage ? "true" : "false") + ",\"isOriginal\":" + (t->isOriginal ? "true" : "false") +
               ",\"x\":" + number(t->x) + ",\"y\":" + number(t->y) + ",\"direction\":" + number(t->direction) +
               ",\"size\":" + number(t->size) + ",\"visible\":" + (t->visible ? "true" : "false") + ",\"variables\":{";
        bool first = true;
        for (const Variable& v : t->variables) {
            if (v.type != Variable::Type::Scalar) continue;
            if (!first) out += ',';
            first = false;
            out += jsonQuote(v.name) + ":" + value(v.value);
        }
        out += "},\"lists\":{";
        first = true;
        for (const Variable& v : t->variables) {
            if (v.type != Variable::Type::List) continue;
            if (!first) out += ',';
            first = false;
            out += jsonQuote(v.name) + ":[";
            for (std::size_t i = 0; i < v.list.size(); i++) out += (i ? "," : "") + value(v.list[i]);
            out += "]";
        }
        out += "}}";
    }
    out += "]}";
    return out;
}

}  // namespace lithium

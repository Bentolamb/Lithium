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

const char* rotationStyleName(RotationStyle r) {
    switch (r) {
        case RotationStyle::AllAround: return "all around";
        case RotationStyle::LeftRight: return "left-right";
        default: return "don't rotate";
    }
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
    // runtime.targets keeps creation order; Project order is layer (executable) order.
    std::vector<std::string> layerOrder;
    for (const auto& t : rt.project().targets()) layerOrder.push_back(t->isStage ? "Stage" : t->name);
    std::sort(order.begin(), order.end(), [](const Target* a, const Target* b) { return a->uid < b->uid; });
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
               ",\"size\":" + number(t->size) + ",\"visible\":" + (t->visible ? "true" : "false") + ",\"costume\":" + std::to_string(t->currentCostume) +
               ",\"volume\":" + number(t->volume) + ",\"rotationStyle\":" + jsonQuote(rotationStyleName(t->rotationStyle)) +
               ",\"effects\":[" + number(t->effects.color) + "," + number(t->effects.fisheye) + "," + number(t->effects.whirl) + "," +
               number(t->effects.pixelate) + "," + number(t->effects.mosaic) + "," + number(t->effects.brightness) + "," +
               number(t->effects.ghost) + "],\"pen\":{\"down\":" + (t->pen.down ? "true" : "false") + ",\"size\":" + number(t->pen.diameter) +
               ",\"color\":[" + number(t->pen.color) + "," + number(t->pen.saturation) + "," + number(t->pen.brightness) + "," +
               number(t->pen.transparency) + "]},\"bubble\":" + jsonQuote(t->bubble.text) + ",\"variables\":{";
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
    out += "],\"layerOrder\":[";
    for (std::size_t i = 0; i < layerOrder.size(); i++) out += (i ? "," : "") + jsonQuote(layerOrder[i]);
    out += "]}";
    return out;
}

}  // namespace lithium

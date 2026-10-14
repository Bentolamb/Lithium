// Color helpers ported from scratch-vm's util/color.js and the color parts of util/cast.js.
#pragma once

#include <string>

#include "lithium/value.h"

namespace lithium {

struct Rgb {
    double r = 0, g = 0, b = 0;
    double a = 255;
    bool hasAlpha = false;
};
struct Hsv {
    double h = 0, s = 0, v = 0;
};

Hsv rgbToHsv(const Rgb& rgb);
Rgb hsvToRgb(const Hsv& hsv);
Rgb mixRgb(const Rgb& a, const Rgb& b, double fractionB);
// Cast.toRgbColorObject: "#rrggbb" strings or decimal numbers.
Rgb toRgbColorObject(const Value& value);

}  // namespace lithium

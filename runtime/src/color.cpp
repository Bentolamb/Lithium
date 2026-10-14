#include "lithium/color.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace lithium {

Hsv rgbToHsv(const Rgb& rgb) {
    const double r = rgb.r / 255, g = rgb.g / 255, b = rgb.b / 255;
    const double x = std::min(std::min(r, g), b);
    const double v = std::max(std::max(r, g), b);
    double h = 0, s = 0;
    if (x != v) {
        const double f = (r == x) ? g - b : ((g == x) ? b - r : r - g);
        const double i = (r == x) ? 3 : ((g == x) ? 5 : 1);
        h = std::fmod((i - (f / (v - x))) * 60, 360.0);
        s = (v - x) / v;
    }
    return {h, s, v};
}

Rgb hsvToRgb(const Hsv& hsv) {
    double h = std::fmod(hsv.h, 360.0);
    if (h < 0) h += 360;
    const double s = std::max(0.0, std::min(hsv.s, 1.0));
    const double v = std::max(0.0, std::min(hsv.v, 1.0));
    const double i = std::floor(h / 60);
    const double f = (h / 60) - i;
    const double p = v * (1 - s);
    const double q = v * (1 - (s * f));
    const double t = v * (1 - (s * (1 - f)));
    double r, g, b;
    switch (static_cast<int>(i)) {
        default:
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        case 5: r = v; g = p; b = q; break;
    }
    Rgb out;
    out.r = std::floor(r * 255);
    out.g = std::floor(g * 255);
    out.b = std::floor(b * 255);
    return out;
}

Rgb mixRgb(const Rgb& a, const Rgb& b, double fractionB) {
    if (fractionB <= 0) return a;
    if (fractionB >= 1) return b;
    const double fa = 1 - fractionB;
    Rgb out;
    out.r = fa * a.r + fractionB * b.r;
    out.g = fa * a.g + fractionB * b.g;
    out.b = fa * a.b + fractionB * b.b;
    return out;
}

namespace {

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

Rgb toRgbColorObject(const Value& value) {
    if (value.isString() && !value.toString().empty() && value.toString()[0] == '#') {
        // Color.hexToRgb: #rgb or #rrggbb (the leading # is optional in the regex).
        std::string hex = value.toString().substr(1);
        std::string full;
        if (hex.size() == 3) {
            for (char c : hex) { full += c; full += c; }
        } else {
            full = hex;
        }
        bool ok = full.size() == 6;
        for (char c : full) ok = ok && hexDigit(c) >= 0;
        if (!ok) {
            Rgb black;
            black.hasAlpha = true;
            black.a = 255;
            return black;
        }
        Rgb out;
        out.r = hexDigit(full[0]) * 16 + hexDigit(full[1]);
        out.g = hexDigit(full[2]) * 16 + hexDigit(full[3]);
        out.b = hexDigit(full[4]) * 16 + hexDigit(full[5]);
        return out;  // no alpha channel
    }
    // Color.decimalToRgb: JavaScript's 32-bit integer conversion of the number.
    const double n = value.toNumber();
    std::int32_t d = 0;
    if (std::isfinite(n)) {
        const double t = std::trunc(n);
        const double m = std::fmod(t, 4294967296.0);
        d = static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::int64_t>(m < 0 ? m + 4294967296.0 : m)));
    }
    const std::uint32_t u = static_cast<std::uint32_t>(d);
    const int a = static_cast<int>((u >> 24) & 0xFF);
    Rgb out;
    out.r = static_cast<double>((u >> 16) & 0xFF);
    out.g = static_cast<double>((u >> 8) & 0xFF);
    out.b = static_cast<double>(u & 0xFF);
    out.a = a > 0 ? a : 255;
    out.hasAlpha = true;
    return out;
}

}  // namespace lithium

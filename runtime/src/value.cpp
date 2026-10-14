#include "lithium/value.h"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace lithium {

namespace {

// Length in bytes of the JavaScript whitespace character starting at `pos`,
// or 0 if there is none. Covers the ECMAScript WhiteSpace and LineTerminator
// sets, encoded as UTF-8.
std::size_t whitespaceLengthAt(std::string_view s, std::size_t pos) {
    const unsigned char c = static_cast<unsigned char>(s[pos]);
    if (c == ' ' || (c >= 9 && c <= 13)) return 1;
    auto byteAt = [&](std::size_t i) -> int {
        return pos + i < s.size() ? static_cast<unsigned char>(s[pos + i]) : -1;
    };
    if (c == 0xC2 && byteAt(1) == 0xA0) return 2;                         // U+00A0
    if (c == 0xE1 && byteAt(1) == 0x9A && byteAt(2) == 0x80) return 3;    // U+1680
    if (c == 0xE2) {
        const int b1 = byteAt(1);
        const int b2 = byteAt(2);
        if (b1 == 0x80 && ((b2 >= 0x80 && b2 <= 0x8A) ||                  // U+2000..200A
                           b2 == 0xA8 || b2 == 0xA9 || b2 == 0xAF)) {     // U+2028/2029/202F
            return 3;
        }
        if (b1 == 0x81 && b2 == 0x9F) return 3;                           // U+205F
    }
    if (c == 0xE3 && byteAt(1) == 0x80 && byteAt(2) == 0x80) return 3;    // U+3000
    if (c == 0xEF && byteAt(1) == 0xBB && byteAt(2) == 0xBF) return 3;    // U+FEFF
    return 0;
}

std::string_view trimJsWhitespace(std::string_view s) {
    std::size_t begin = 0;
    while (begin < s.size()) {
        const std::size_t n = whitespaceLengthAt(s, begin);
        if (n == 0) break;
        begin += n;
    }
    std::size_t end = s.size();
    while (end > begin) {
        bool stripped = false;
        for (std::size_t len = 3; len >= 1; --len) {
            if (end - begin >= len && whitespaceLengthAt(s, end - len) == len) {
                end -= len;
                stripped = true;
                break;
            }
        }
        if (!stripped) break;
    }
    return s.substr(begin, end - begin);
}

std::string asciiLower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }

}  // namespace

double jsRound(double x) {
    if (!std::isfinite(x)) return x;
    const double r = std::floor(x);
    return (x - r >= 0.5) ? r + 1.0 : r;
}

bool isJsWhitespaceOnly(std::string_view s) { return trimJsWhitespace(s).empty(); }

double stringToJsNumber(std::string_view input) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const std::string_view t = trimJsWhitespace(input);
    if (t.empty()) return 0.0;

    // 0x / 0o / 0b literals (unsigned only), as in JavaScript.
    if (t.size() > 2 && t[0] == '0') {
        int base = 0;
        switch (t[1]) {
            case 'x': case 'X': base = 16; break;
            case 'o': case 'O': base = 8; break;
            case 'b': case 'B': base = 2; break;
            default: break;
        }
        if (base != 0) {
            double acc = 0.0;
            for (std::size_t i = 2; i < t.size(); ++i) {
                const char c = t[i];
                int d;
                if (c >= '0' && c <= '9') d = c - '0';
                else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
                else return nan;
                if (d >= base) return nan;
                acc = acc * base + d;
            }
            return acc;
        }
    }

    // Decimal literal: [+-] (Infinity | digits [. digits?] | . digits) [e [+-] digits]
    std::size_t i = 0;
    bool negative = false;
    if (t[i] == '+' || t[i] == '-') {
        negative = (t[i] == '-');
        ++i;
    }
    if (t.substr(i) == "Infinity") return negative ? -inf : inf;

    const std::size_t numberStart = i;
    std::size_t intDigits = 0;
    std::size_t fracDigits = 0;
    while (i < t.size() && isDigit(t[i])) { ++i; ++intDigits; }
    if (i < t.size() && t[i] == '.') {
        ++i;
        while (i < t.size() && isDigit(t[i])) { ++i; ++fracDigits; }
    }
    if (intDigits + fracDigits == 0) return nan;
    if (i < t.size() && (t[i] == 'e' || t[i] == 'E')) {
        ++i;
        if (i < t.size() && (t[i] == '+' || t[i] == '-')) ++i;
        std::size_t expDigits = 0;
        while (i < t.size() && isDigit(t[i])) { ++i; ++expDigits; }
        if (expDigits == 0) return nan;
    }
    if (i != t.size()) return nan;

    // Syntax is validated above, so strtod only has to do the conversion. It
    // also yields +/-HUGE_VAL on overflow and 0 on underflow, like JavaScript.
    const std::string text(t.substr(numberStart));
    const double magnitude = std::strtod(text.c_str(), nullptr);
    return negative ? -magnitude : magnitude;
}

std::string numberToJsString(double v) {
    if (std::isnan(v)) return "NaN";
    if (v == 0.0) return "0";
    if (std::isinf(v)) return v < 0 ? "-Infinity" : "Infinity";

    // Shortest round-trip digits, then lay them out following the ECMAScript
    // Number::toString rules.
    char buf[64];
    const auto res = std::to_chars(buf, buf + sizeof buf, std::fabs(v), std::chars_format::scientific);
    const std::string sci(buf, res.ptr);
    const std::size_t ePos = sci.find('e');
    const std::string mantissa = sci.substr(0, ePos);
    const int exp10 = std::stoi(sci.substr(ePos + 1));

    std::string digits;
    for (char c : mantissa) {
        if (c != '.') digits.push_back(c);
    }
    const int k = static_cast<int>(digits.size());
    const int n = exp10 + 1;

    std::string out;
    if (v < 0) out.push_back('-');
    if (k <= n && n <= 21) {
        out += digits;
        out.append(static_cast<std::size_t>(n - k), '0');
    } else if (0 < n && n <= 21) {
        out += digits.substr(0, static_cast<std::size_t>(n));
        out.push_back('.');
        out += digits.substr(static_cast<std::size_t>(n));
    } else if (-6 < n && n <= 0) {
        out += "0.";
        out.append(static_cast<std::size_t>(-n), '0');
        out += digits;
    } else {
        const int e = n - 1;
        out.push_back(digits[0]);
        if (k > 1) {
            out.push_back('.');
            out += digits.substr(1);
        }
        out.push_back('e');
        out.push_back(e < 0 ? '-' : '+');
        out += std::to_string(e < 0 ? -e : e);
    }
    return out;
}

double Value::toJsNumber() const {
    if (const double* d = std::get_if<double>(&data_)) return *d;
    if (const bool* b = std::get_if<bool>(&data_)) return *b ? 1.0 : 0.0;
    return stringToJsNumber(std::get<std::string>(data_));
}

double Value::toNumber() const {
    const double n = toJsNumber();
    return std::isnan(n) ? 0.0 : n;
}

bool Value::toBoolean() const {
    if (const bool* b = std::get_if<bool>(&data_)) return *b;
    if (const double* d = std::get_if<double>(&data_)) return *d != 0.0 && !std::isnan(*d);
    const std::string& s = std::get<std::string>(data_);
    if (s.empty() || s == "0") return false;
    return asciiLower(s) != "false";
}

std::string Value::toString() const {
    if (const std::string* s = std::get_if<std::string>(&data_)) return *s;
    if (const bool* b = std::get_if<bool>(&data_)) return *b ? "true" : "false";
    return numberToJsString(std::get<double>(data_));
}

bool Value::isWhiteSpace(const Value& v) {
    const std::string* s = std::get_if<std::string>(&v.data_);
    return s != nullptr && isJsWhitespaceOnly(*s);
}

double Value::compare(const Value& v1, const Value& v2) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    double n1 = v1.toJsNumber();
    double n2 = v2.toJsNumber();
    // Same quirk as Cast.compare: only one side is checked, first side wins.
    if (n1 == 0.0 && isWhiteSpace(v1)) {
        n1 = nan;
    } else if (n2 == 0.0 && isWhiteSpace(v2)) {
        n2 = nan;
    }
    if (std::isnan(n1) || std::isnan(n2)) {
        const std::string s1 = asciiLower(v1.toString());
        const std::string s2 = asciiLower(v2.toString());
        const int c = s1.compare(s2);
        return c < 0 ? -1.0 : (c > 0 ? 1.0 : 0.0);
    }
    if ((n1 == inf && n2 == inf) || (n1 == -inf && n2 == -inf)) return 0.0;
    return n1 - n2;
}

bool Value::identical(const Value& other) const {
    if (data_.index() != other.data_.index()) return false;
    if (const double* a = std::get_if<double>(&data_)) {
        const double b = std::get<double>(other.data_);
        return (std::isnan(*a) && std::isnan(b)) || *a == b;
    }
    if (const bool* a = std::get_if<bool>(&data_)) return *a == std::get<bool>(other.data_);
    return std::get<std::string>(data_) == std::get<std::string>(other.data_);
}

}  // namespace lithium

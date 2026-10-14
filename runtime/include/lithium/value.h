// Scratch value semantics.
//
// A Scratch value is a number, a string or a boolean. All coercion rules in
// this file mirror scratch-vm's util/cast.js (Cast.toNumber, Cast.toBoolean,
// Cast.toString, Cast.compare), including JavaScript's own Number(string) and
// String(number) behaviour, which Scratch inherits.
//
// Known gap: case-insensitive comparison only folds ASCII letters.
#pragma once

#include <string>
#include <string_view>
#include <variant>

namespace lithium {

// JavaScript-compatible helpers, exposed for tests and the project loader.
double jsRound(double x);                      // Math.round (ties toward +Infinity)
double stringToJsNumber(std::string_view s);   // Number(string); NaN if invalid
std::string numberToJsString(double v);        // String(number)
bool isJsWhitespaceOnly(std::string_view s);   // s.trim().length === 0

class Value {
public:
    Value() : data_(0.0) {}

    static Value number(double d) {
        Value v;
        v.data_.emplace<double>(d);
        return v;
    }
    static Value boolean(bool b) {
        Value v;
        v.data_.emplace<bool>(b);
        return v;
    }
    static Value string(std::string s) {
        Value v;
        v.data_.emplace<std::string>(std::move(s));
        return v;
    }

    bool isNumber() const { return std::holds_alternative<double>(data_); }
    bool isBoolean() const { return std::holds_alternative<bool>(data_); }
    bool isString() const { return std::holds_alternative<std::string>(data_); }

    // Cast.toNumber: NaN becomes 0.
    double toNumber() const;
    // Cast.toBoolean: "", "0" and "false" (any case) are false.
    bool toBoolean() const;
    // Cast.toString.
    std::string toString() const;
    // Number(value): NaN when the value is not numeric. Used by compare.
    double toJsNumber() const;

    // Cast.compare: <0, 0, >0. Case-insensitive string comparison when either
    // side is not numeric; whitespace-only strings are not numbers.
    static double compare(const Value& a, const Value& b);
    static bool isWhiteSpace(const Value& v);

    // Same type and same content (NaN is identical to NaN). For tests.
    bool identical(const Value& other) const;

private:
    std::variant<double, bool, std::string> data_;
};

}  // namespace lithium

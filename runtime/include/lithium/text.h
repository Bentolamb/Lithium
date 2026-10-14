// Text helpers with JavaScript semantics.
//
// Scratch strings are JavaScript strings: UTF-16 code units, compared and
// measured as such. Lithium stores text as UTF-8; a lone surrogate (which
// JavaScript allows and UTF-8 does not) is stored in its 3-byte "WTF-8" form.
#pragma once

#include <string>

namespace lithium {

std::u16string utf8ToUtf16(const std::string& s);
std::string utf16ToUtf8(const std::u16string& s);

// String.prototype.toLowerCase / toUpperCase, full Unicode mapping.
std::string jsToLowerCase(const std::string& s);
std::string jsToUpperCase(const std::string& s);

// Lexicographic comparison by UTF-16 code units (the JavaScript `<` on strings).
int compareUtf16(const std::string& a, const std::string& b);

// a + b, merging a trailing high surrogate of `a` with a leading low
// surrogate of `b` into one code point, as JavaScript concatenation does.
std::string concatJs(const std::string& a, const std::string& b);

// JSON string literal (with quotes) for a WTF-8 string; lone surrogates become \uXXXX.
std::string jsonQuote(const std::string& s);

}  // namespace lithium

#include "lithium/text.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace lithium {

namespace {

struct CaseEntry {
    std::uint32_t cp;
    std::uint32_t m[3];
};

#include "unicode_case_data.inc"

// Decodes WTF-8 into code points (surrogates allowed). Invalid bytes become U+FFFD.
std::vector<std::uint32_t> decode(const std::string& s) {
    std::vector<std::uint32_t> out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        std::uint32_t cp;
        int len;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c >> 5) == 0x6) { cp = c & 0x1F; len = 2; }
        else if ((c >> 4) == 0xE) { cp = c & 0x0F; len = 3; }
        else if ((c >> 3) == 0x1E) { cp = c & 0x07; len = 4; }
        else { out.push_back(0xFFFD); i++; continue; }
        if (i + static_cast<std::size_t>(len) > s.size()) { out.push_back(0xFFFD); break; }
        for (int k = 1; k < len; k++) cp = (cp << 6) | (static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]) & 0x3F);
        i += static_cast<std::size_t>(len);
        out.push_back(cp);
    }
    return out;
}

void encodeOne(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) out.push_back(static_cast<char>(cp));
    else if (cp < 0x800) { out.push_back(static_cast<char>(0xC0 | (cp >> 6))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) { out.push_back(static_cast<char>(0xE0 | (cp >> 12))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
    else { out.push_back(static_cast<char>(0xF0 | (cp >> 18))); out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
}

// Re-encodes code points, joining adjacent surrogate halves into one code point.
std::string encode(const std::vector<std::uint32_t>& cps) {
    std::string out;
    out.reserve(cps.size());
    for (std::size_t i = 0; i < cps.size(); i++) {
        std::uint32_t cp = cps[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < cps.size() && cps[i + 1] >= 0xDC00 && cps[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (cps[i + 1] - 0xDC00);
            i++;
        }
        encodeOne(out, cp);
    }
    return out;
}

template <std::size_t N>
const CaseEntry* findEntry(const CaseEntry (&table)[N], std::uint32_t cp) {
    const CaseEntry* end = table + N;
    const CaseEntry* it = std::lower_bound(table, end, cp, [](const CaseEntry& e, std::uint32_t c) { return e.cp < c; });
    return (it != end && it->cp == cp) ? it : nullptr;
}

// Approximations of the Unicode "Cased" and "Case_Ignorable" properties,
// used only for the final-sigma rule.
bool isCased(std::uint32_t cp) {
    if (cp < 0x80) return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z');
    return findEntry(kLowerTable, cp) != nullptr || findEntry(kUpperTable, cp) != nullptr;
}
bool isCaseIgnorable(std::uint32_t cp) {
    switch (cp) {
        case '\'': case '.': case ':': case '^': case '`': case 0xA8: case 0xAD: case 0xAF: case 0xB4: case 0xB7: case 0xB8:
        case 0x2018: case 0x2019: case 0x2024: case 0x2027: case 0xFE13: case 0xFE52: case 0xFE55: case 0xFF07: case 0xFF0E: case 0xFF1A:
            return true;
        default: break;
    }
    return (cp >= 0x300 && cp <= 0x36F) || (cp >= 0x1AB0 && cp <= 0x1AFF) || (cp >= 0x1DC0 && cp <= 0x1DFF) ||
           (cp >= 0x20D0 && cp <= 0x20FF) || (cp >= 0xFE20 && cp <= 0xFE2F) || (cp >= 0x200B && cp <= 0x200F) ||
           (cp >= 0x2B0 && cp <= 0x2FF);
}

std::string mapCase(const std::string& s, bool lower) {
    // ASCII fast path.
    bool ascii = true;
    for (char c : s) if (static_cast<unsigned char>(c) >= 0x80) { ascii = false; break; }
    if (ascii) {
        std::string out = s;
        for (char& c : out) {
            if (lower && c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            else if (!lower && c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        }
        return out;
    }
    const std::vector<std::uint32_t> in = decode(s);
    std::vector<std::uint32_t> out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); i++) {
        const std::uint32_t cp = in[i];
        if (lower && cp == 0x3A3) {
            // Final sigma: preceded by a cased letter, not followed by one
            // (skipping case-ignorable characters on both sides).
            bool before = false, after = false;
            for (std::size_t j = i; j-- > 0;) { if (isCaseIgnorable(in[j])) continue; before = isCased(in[j]); break; }
            for (std::size_t j = i + 1; j < in.size(); j++) { if (isCaseIgnorable(in[j])) continue; after = isCased(in[j]); break; }
            out.push_back(before && !after ? 0x3C2 : 0x3C3);
            continue;
        }
        const CaseEntry* e = lower ? findEntry(kLowerTable, cp) : findEntry(kUpperTable, cp);
        if (!e) { out.push_back(cp); continue; }
        for (int k = 0; k < 3 && e->m[k] != 0; k++) out.push_back(e->m[k]);
    }
    return encode(out);
}

}  // namespace

std::u16string utf8ToUtf16(const std::string& s) {
    std::u16string out;
    out.reserve(s.size());
    for (std::uint32_t cp : decode(s)) {
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(cp));
        }
    }
    return out;
}

std::string utf16ToUtf8(const std::u16string& s) {
    std::vector<std::uint32_t> cps(s.begin(), s.end());
    return encode(cps);
}

std::string jsToLowerCase(const std::string& s) { return mapCase(s, true); }
std::string jsToUpperCase(const std::string& s) { return mapCase(s, false); }

int compareUtf16(const std::string& a, const std::string& b) {
    // Pure-ASCII strings compare identically as bytes.
    const std::u16string x = utf8ToUtf16(a), y = utf8ToUtf16(b);
    const int c = x.compare(y);
    return c < 0 ? -1 : (c > 0 ? 1 : 0);
}

std::string concatJs(const std::string& a, const std::string& b) {
    // A high surrogate ends in ED A0..AF xx; a low surrogate starts with ED B0..BF xx.
    if (a.size() >= 3 && b.size() >= 3) {
        const unsigned char a0 = static_cast<unsigned char>(a[a.size() - 3]);
        const unsigned char a1 = static_cast<unsigned char>(a[a.size() - 2]);
        const unsigned char b0 = static_cast<unsigned char>(b[0]);
        const unsigned char b1 = static_cast<unsigned char>(b[1]);
        if (a0 == 0xED && a1 >= 0xA0 && a1 <= 0xAF && b0 == 0xED && b1 >= 0xB0 && b1 <= 0xBF) {
            const std::string joined = a.substr(a.size() - 3) + b.substr(0, 3);
            return a.substr(0, a.size() - 3) + encode(decode(joined)) + b.substr(3);
        }
    }
    return a + b;
}

std::string jsonQuote(const std::string& s) {
    std::string out = "\"";
    for (std::uint32_t cp : decode(s)) {
        switch (cp) {
            case '"': out += "\\\""; continue;
            case '\\': out += "\\\\"; continue;
            case '\n': out += "\\n"; continue;
            case '\r': out += "\\r"; continue;
            case '\t': out += "\\t"; continue;
            default: break;
        }
        if (cp < 0x20 || (cp >= 0xD800 && cp <= 0xDFFF)) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(cp));
            out += buf;
        } else {
            encodeOne(out, cp);
        }
    }
    out += '"';
    return out;
}

}  // namespace lithium

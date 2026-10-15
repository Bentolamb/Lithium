#include "lithium/outlist.h"

#include <cmath>
#include <cstring>

#include "lithium/packin.h"

namespace lithium::outlist {

namespace {

void putVarint(std::string& out, std::uint64_t v) {
    while (v >= 0x80) {
        out.push_back(static_cast<char>((v & 0x7f) | 0x80));
        v >>= 7;
    }
    out.push_back(static_cast<char>(v));
}

bool getVarint(std::string_view s, std::size_t& pos, std::uint64_t& out) {
    out = 0;
    for (int shift = 0; shift < 64; shift += 7) {
        if (pos >= s.size()) return false;
        const auto b = static_cast<std::uint8_t>(s[pos++]);
        out |= static_cast<std::uint64_t>(b & 0x7f) << shift;
        if ((b & 0x80) == 0) return true;
    }
    return false;
}

bool numericText(double d) {
    if (!std::isfinite(d) || (d == 0 && std::signbit(d))) return false;
    for (char c : numberToJsString(d)) {
        if (!((c >= '0' && c <= '9') || c == '-' || c == '.')) return false;
    }
    return true;
}

struct ChunkOut {
    int kind;
    std::uint64_t items;
    std::string payload;
};

ChunkOut encodeChunk(const std::vector<Value>& items, std::size_t begin, std::size_t end) {
    bool allNumeric = true;
    for (std::size_t i = begin; i < end && allNumeric; i++) allNumeric = items[i].isNumber() && numericText(items[i].toNumber());
    std::string raw;
    int kind = 0;
    if (allNumeric) {
        kind = 1;
        for (std::size_t i = begin; i < end; i++) {
            if (i > begin) raw.push_back('\n');
            raw += numberToJsString(items[i].toNumber());
        }
    } else {
        for (std::size_t i = begin; i < end; i++) {
            const Value& v = items[i];
            if (v.isNumber()) {
                raw.push_back(1);
                const double d = v.toNumber();
                std::uint64_t bits;
                std::memcpy(&bits, &d, 8);
                for (int k = 0; k < 8; k++) raw.push_back(static_cast<char>((bits >> (8 * k)) & 0xff));
            } else if (v.isBoolean()) {
                raw.push_back(v.toBoolean() ? 2 : 3);
            } else {
                const std::string s = v.toString();
                raw.push_back(0);
                putVarint(raw, s.size());
                raw += s;
            }
        }
    }
    return {kind, end - begin, packin::compress(raw)};
}

}  // namespace

std::string validateName(std::string_view name) {
    if (name.empty()) return "name is empty";
    if (name.size() > kMaxNameBytes) return "name is longer than 100 bytes";
    for (unsigned char c : name) {
        if (c < 0x20 || c == 0x7f) return "name contains a control character";
        switch (c) {
            case '/': case '\\': case ':': case '*': case '?': case '"': case '<': case '>': case '|': case '%':
                return std::string("name contains the character '") + static_cast<char>(c) + "'";
            default: break;
        }
    }
    if (name.front() == ' ' || name.back() == ' ') return "name starts or ends with a space";
    if (name.front() == '.' || name.back() == '.') return "name starts or ends with a dot";
    // must be well-formed UTF-8
    for (std::size_t i = 0; i < name.size();) {
        const auto c = static_cast<unsigned char>(name[i]);
        const int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (n == 0 || i + static_cast<std::size_t>(n) > name.size()) return "name is not valid text";
        for (int k = 1; k < n; k++) if ((static_cast<unsigned char>(name[i + static_cast<std::size_t>(k)]) & 0xC0) != 0x80) return "name is not valid text";
        i += static_cast<std::size_t>(n);
    }
    return {};
}

std::string encode(const std::vector<Value>& items) {
    std::vector<ChunkOut> chunks;
    for (std::size_t b = 0; b < items.size(); b += kChunkItems) chunks.push_back(encodeChunk(items, b, std::min(items.size(), b + kChunkItems)));
    std::string head = "OUTL";
    head.push_back(1);
    head.push_back(0);
    putVarint(head, items.size());
    putVarint(head, kChunkItems);
    putVarint(head, chunks.size());
    for (const ChunkOut& c : chunks) {
        head.push_back(static_cast<char>(c.kind));
        putVarint(head, c.items);
        putVarint(head, c.payload.size());
    }
    const std::uint32_t crc = packin::crc32(head);
    for (int i = 0; i < 4; i++) head.push_back(static_cast<char>((crc >> (8 * i)) & 0xff));
    for (const ChunkOut& c : chunks) head += c.payload;
    return head;
}

namespace {

struct ChunkRef {
    int kind;
    std::uint64_t items;
    std::uint64_t bytes;
};
struct Parsed {
    std::uint64_t itemCount = 0;
    std::vector<ChunkRef> chunks;
    std::size_t payloadStart = 0;
};

bool parseHeader(std::string_view f, Parsed& p, std::string& error) {
    if (f.size() < 6 || f.substr(0, 4) != "OUTL") { error = "not an OutList file"; return false; }
    if (static_cast<unsigned char>(f[4]) != 1) { error = "unsupported OutList file version"; return false; }
    if (f[5] != 0) { error = "unsupported OutList file flags"; return false; }
    std::size_t pos = 6;
    std::uint64_t chunkItems, chunkCount;
    if (!getVarint(f, pos, p.itemCount) || !getVarint(f, pos, chunkItems) || !getVarint(f, pos, chunkCount)) { error = "damaged header"; return false; }
    if (p.itemCount > kMaxItems || chunkItems == 0 || chunkCount > kMaxItems) { error = "damaged header"; return false; }
    std::uint64_t total = 0;
    for (std::uint64_t i = 0; i < chunkCount; i++) {
        if (pos >= f.size()) { error = "damaged header"; return false; }
        ChunkRef c;
        c.kind = static_cast<unsigned char>(f[pos++]);
        if (!getVarint(f, pos, c.items) || !getVarint(f, pos, c.bytes) || c.kind > 1 || c.items == 0 || c.items > chunkItems) { error = "damaged header"; return false; }
        total += c.items;
        p.chunks.push_back(c);
    }
    if (total != p.itemCount) { error = "damaged header"; return false; }
    if (pos + 4 > f.size()) { error = "damaged header"; return false; }
    std::uint32_t stored = 0;
    for (int i = 0; i < 4; i++) stored |= static_cast<std::uint32_t>(static_cast<unsigned char>(f[pos + static_cast<std::size_t>(i)])) << (8 * i);
    if (packin::crc32(f.substr(0, pos)) != stored) { error = "damaged header (checksum mismatch)"; return false; }
    p.payloadStart = pos + 4;
    std::uint64_t bytes = 0;
    for (const ChunkRef& c : p.chunks) bytes += c.bytes;
    if (p.payloadStart + bytes != f.size()) { error = f.size() < p.payloadStart + bytes ? "file is truncated" : "unexpected data after the last chunk"; return false; }
    return true;
}

}  // namespace

std::optional<Info> readInfo(std::string_view file, std::string& error) {
    Parsed p;
    if (!parseHeader(file, p, error)) return std::nullopt;
    return Info{p.itemCount, p.chunks.size()};
}

std::optional<std::vector<Value>> decode(std::string_view file, std::string& error) {
    Parsed p;
    if (!parseHeader(file, p, error)) return std::nullopt;
    std::vector<Value> items;
    items.reserve(static_cast<std::size_t>(p.itemCount));
    std::size_t pos = p.payloadStart;
    for (const ChunkRef& c : p.chunks) {
        const auto raw = packin::decompress(file.substr(pos, static_cast<std::size_t>(c.bytes)));
        pos += static_cast<std::size_t>(c.bytes);
        if (!raw) { error = "damaged data (a chunk failed its checksum)"; return std::nullopt; }
        const std::size_t before = items.size();
        if (c.kind == 1) {
            std::size_t start = 0;
            for (std::size_t i = 0; i <= raw->size(); i++) {
                if (i == raw->size() || (*raw)[i] == '\n') {
                    const std::string tok = raw->substr(start, i - start);
                    if (tok.empty()) { error = "damaged data"; return std::nullopt; }
                    items.push_back(Value::number(Value::string(tok).toJsNumber()));
                    start = i + 1;
                }
            }
        } else {
            std::size_t i = 0;
            const std::string& r = *raw;
            while (i < r.size()) {
                const int tag = static_cast<unsigned char>(r[i++]);
                if (tag == 0) {
                    std::uint64_t len;
                    if (!getVarint(r, i, len) || len > r.size() - i) { error = "damaged data"; return std::nullopt; }
                    items.push_back(Value::string(r.substr(i, static_cast<std::size_t>(len))));
                    i += static_cast<std::size_t>(len);
                } else if (tag == 1) {
                    if (r.size() - i < 8) { error = "damaged data"; return std::nullopt; }
                    std::uint64_t bits = 0;
                    for (int k = 0; k < 8; k++) bits |= static_cast<std::uint64_t>(static_cast<unsigned char>(r[i + static_cast<std::size_t>(k)])) << (8 * k);
                    i += 8;
                    double d;
                    std::memcpy(&d, &bits, 8);
                    items.push_back(Value::number(d));
                } else if (tag == 2 || tag == 3) {
                    items.push_back(Value::boolean(tag == 2));
                } else {
                    error = "damaged data";
                    return std::nullopt;
                }
            }
        }
        if (items.size() - before != c.items) { error = "damaged data (item count mismatch)"; return std::nullopt; }
    }
    return items;
}

std::uint64_t fingerprint(const std::vector<Value>& items) {
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&h](unsigned char b) { h = (h ^ b) * 1099511628211ull; };
    for (const Value& v : items) {
        if (v.isNumber()) {
            mix(1);
            const double d = v.toNumber();
            std::uint64_t bits;
            std::memcpy(&bits, &d, 8);
            for (int k = 0; k < 8; k++) mix(static_cast<unsigned char>(bits >> (8 * k)));
        } else if (v.isBoolean()) {
            mix(v.toBoolean() ? 2 : 3);
        } else {
            mix(0);
            for (unsigned char c : v.toString()) mix(c);
            mix(0xff);
        }
    }
    return h ^ (items.size() * 0x9E3779B97F4A7C15ull);
}

}  // namespace lithium::outlist

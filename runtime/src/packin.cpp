#include "lithium/packin.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <queue>
#include <vector>

namespace lithium::packin {

namespace {

// ---------------------------------------------------------------- utilities

constexpr std::uint64_t kMaxOriginal = 1ull << 30;  // refuse to decode into more than 1 GiB

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
        const std::uint8_t b = static_cast<std::uint8_t>(s[pos++]);
        out |= static_cast<std::uint64_t>(b & 0x7f) << shift;
        if ((b & 0x80) == 0) return true;
    }
    return false;
}

std::uint64_t zigzag(std::int64_t v) { return (static_cast<std::uint64_t>(v) << 1) ^ static_cast<std::uint64_t>(v >> 63); }
std::int64_t unzigzag(std::uint64_t v) { return static_cast<std::int64_t>(v >> 1) ^ -static_cast<std::int64_t>(v & 1); }

// ----------------------------------------------------------------- bit I/O

class BitWriter {
public:
    void put(std::uint32_t value, int bits) {  // LSB first
        acc_ |= static_cast<std::uint64_t>(value) << n_;
        n_ += bits;
        while (n_ >= 8) {
            out_.push_back(static_cast<char>(acc_ & 0xff));
            acc_ >>= 8;
            n_ -= 8;
        }
    }
    std::string finish() {
        if (n_ > 0) out_.push_back(static_cast<char>(acc_ & 0xff));
        acc_ = 0;
        n_ = 0;
        return std::move(out_);
    }

private:
    std::string out_;
    std::uint64_t acc_ = 0;
    int n_ = 0;
};

class BitReader {
public:
    explicit BitReader(std::string_view s) : s_(s) {}
    // Ensures at least 32 bits are buffered where input remains (zero padded at the end).
    void refill() {
        while (n_ <= 56) {
            std::uint64_t b = pos_ < s_.size() ? static_cast<std::uint8_t>(s_[pos_]) : 0;
            if (pos_ >= s_.size()) overrun_++;
            else pos_++;
            acc_ |= b << n_;
            n_ += 8;
        }
    }
    std::uint32_t peek(int bits) const { return static_cast<std::uint32_t>(acc_ & ((1ull << bits) - 1)); }
    void drop(int bits) {
        acc_ >>= bits;
        n_ -= bits;
    }
    std::uint32_t get(int bits) {
        refill();
        const std::uint32_t v = peek(bits);
        drop(bits);
        return v;
    }
    // True when more bits were consumed than the input holds.
    bool exhausted() const { return overrun_ * 8 > static_cast<std::size_t>(n_); }

private:
    std::string_view s_;
    std::size_t pos_ = 0;
    std::uint64_t acc_ = 0;
    int n_ = 0;
    std::size_t overrun_ = 0;
};

// ----------------------------------------------------------------- tables

constexpr int kLitLen = 286;  // 0..255 literals, 256 end of block, 257..285 lengths
constexpr int kDist = 30;
constexpr int kEnd = 256;
constexpr int kMaxBits = 15;
constexpr int kWindow = 32768;
constexpr int kMinMatch = 3;
constexpr int kMaxMatch = 258;

constexpr std::array<std::uint16_t, 29> kLenBase = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<std::uint8_t, 29> kLenExtra = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<std::uint16_t, 30> kDistBase = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<std::uint8_t, 30> kDistExtra = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

int lengthCode(int len) {  // len in 3..258 -> 0..28
    int c = 28;
    while (kLenBase[static_cast<std::size_t>(c)] > len) c--;
    return c;
}
int distCode(int dist) {  // dist in 1..32768 -> 0..29
    int c = 29;
    while (kDistBase[static_cast<std::size_t>(c)] > dist) c--;
    return c;
}

// ------------------------------------------------------------- huffman codes

// Code lengths (<= kMaxBits) for the given frequencies. Symbols with frequency 0 get 0.
std::vector<std::uint8_t> buildLengths(const std::vector<std::uint32_t>& freq) {
    const std::size_t n = freq.size();
    std::vector<std::uint8_t> lens(n, 0);
    std::vector<std::uint64_t> f(freq.begin(), freq.end());
    for (;;) {
        struct Node { std::uint64_t w; int left, right; };
        std::vector<Node> nodes;
        using Item = std::pair<std::uint64_t, int>;
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
        for (std::size_t i = 0; i < n; i++) {
            if (f[i] > 0) {
                nodes.push_back({f[i], -1 - static_cast<int>(i), 0});
                pq.push({f[i], static_cast<int>(nodes.size()) - 1});
            }
        }
        std::fill(lens.begin(), lens.end(), 0);
        if (nodes.empty()) return lens;
        if (nodes.size() == 1) {
            lens[static_cast<std::size_t>(-1 - nodes[0].left)] = 1;
            return lens;
        }
        while (pq.size() > 1) {
            const Item a = pq.top(); pq.pop();
            const Item b = pq.top(); pq.pop();
            nodes.push_back({a.first + b.first, a.second, b.second});
            pq.push({a.first + b.first, static_cast<int>(nodes.size()) - 1});
        }
        // depth-first walk from the root
        int maxLen = 0;
        std::vector<std::pair<int, int>> stack{{pq.top().second, 0}};
        while (!stack.empty()) {
            const auto [idx, depth] = stack.back();
            stack.pop_back();
            const Node& nd = nodes[static_cast<std::size_t>(idx)];
            if (nd.left < 0) {
                // leaf (leaves are the first nodes pushed and carry a negative symbol reference)
                lens[static_cast<std::size_t>(-1 - nd.left)] = static_cast<std::uint8_t>(depth);
                maxLen = std::max(maxLen, depth);
            } else {
                stack.push_back({nd.left, depth + 1});
                stack.push_back({nd.right, depth + 1});
            }
        }
        if (maxLen <= kMaxBits) return lens;
        // Too deep: flatten the distribution and rebuild.
        for (auto& w : f) {
            if (w > 0) w = (w + 1) / 2;
        }
    }
}

// Canonical codes, stored bit-reversed so they can be written LSB first.
std::vector<std::uint16_t> buildCodes(const std::vector<std::uint8_t>& lens) {
    std::array<int, kMaxBits + 2> count{};
    for (auto l : lens) count[l]++;
    count[0] = 0;
    std::array<int, kMaxBits + 2> next{};
    int code = 0;
    for (int b = 1; b <= kMaxBits; b++) {
        code = (code + count[static_cast<std::size_t>(b - 1)]) << 1;
        next[static_cast<std::size_t>(b)] = code;
    }
    std::vector<std::uint16_t> codes(lens.size(), 0);
    for (std::size_t i = 0; i < lens.size(); i++) {
        const int l = lens[i];
        if (l == 0) continue;
        int c = next[static_cast<std::size_t>(l)]++;
        int r = 0;
        for (int k = 0; k < l; k++) {
            r = (r << 1) | (c & 1);
            c >>= 1;
        }
        codes[i] = static_cast<std::uint16_t>(r);
    }
    return codes;
}

// ----------------------------------------------------------- decoding table

constexpr int kFastBits = 10;

struct Decoder {
    std::array<std::uint16_t, 1 << kFastBits> fast{};  // (symbol << 4) | length, 0 = not in table
    std::array<int, kMaxBits + 2> count{};
    std::array<int, kMaxBits + 2> firstCode{};
    std::array<int, kMaxBits + 2> firstIndex{};
    std::vector<std::uint16_t> sorted;  // symbols ordered by (length, symbol)
    bool empty = true;

    bool build(const std::vector<std::uint8_t>& lens) {
        fast.fill(0);
        count.fill(0);
        sorted.clear();
        int used = 0;
        for (auto l : lens) {
            if (l > kMaxBits) return false;
            count[l]++;
            if (l) used++;
        }
        count[0] = 0;
        empty = used == 0;
        // oversubscribed codes are invalid
        int left = 1;
        for (int b = 1; b <= kMaxBits; b++) {
            left <<= 1;
            left -= count[static_cast<std::size_t>(b)];
            if (left < 0) return false;
        }
        int code = 0, index = 0;
        for (int b = 1; b <= kMaxBits; b++) {
            code = (code + count[static_cast<std::size_t>(b - 1)]) << 1;
            firstCode[static_cast<std::size_t>(b)] = code;
            firstIndex[static_cast<std::size_t>(b)] = index;
            index += count[static_cast<std::size_t>(b)];
        }
        sorted.resize(static_cast<std::size_t>(index));
        std::array<int, kMaxBits + 2> fill = firstIndex;
        for (std::size_t s = 0; s < lens.size(); s++) {
            if (lens[s]) sorted[static_cast<std::size_t>(fill[lens[s]]++)] = static_cast<std::uint16_t>(s);
        }
        const auto codes = buildCodes(lens);
        for (std::size_t s = 0; s < lens.size(); s++) {
            const int l = lens[s];
            if (l == 0 || l > kFastBits) continue;
            for (std::size_t e = codes[s]; e < fast.size(); e += static_cast<std::size_t>(1) << l) {
                fast[e] = static_cast<std::uint16_t>((s << 4) | static_cast<std::size_t>(l));
            }
        }
        return true;
    }

    // Returns the symbol or -1 on an invalid code. Needs >= kMaxBits bits buffered.
    int decode(BitReader& br) const {
        const std::uint32_t bits = br.peek(kMaxBits);
        const std::uint16_t f = fast[bits & ((1u << kFastBits) - 1)];
        if (f != 0) {
            br.drop(f & 15);
            return f >> 4;
        }
        int code = 0;
        for (int l = 1; l <= kMaxBits; l++) {
            code = (code << 1) | static_cast<int>((bits >> (l - 1)) & 1);
            const int c = count[static_cast<std::size_t>(l)];
            const int rel = code - firstCode[static_cast<std::size_t>(l)];
            if (c > 0 && rel >= 0 && rel < c) {
                br.drop(l);
                return sorted[static_cast<std::size_t>(firstIndex[static_cast<std::size_t>(l)] + rel)];
            }
        }
        return -1;
    }
};

// -------------------------------------------------------------- LZ77 matcher

struct Token {
    std::uint16_t lenOrLit;  // literal byte (dist == 0) or match length
    std::uint16_t dist;      // 0 for literals
};

struct LevelParams {
    int chain;
    bool lazy;
    int nice;
};

LevelParams paramsFor(int level) {
    static const LevelParams table[] = {{0, false, 0}, {4, false, 16}, {8, false, 24}, {16, false, 32}, {32, true, 48},
                                        {64, true, 64}, {128, true, 96}, {256, true, 128}, {1024, true, 200}, {4096, true, 258}};
    return table[std::clamp(level, kMinLevel, kMaxLevel)];
}

class Matcher {
public:
    Matcher(std::string_view data, const LevelParams& p) : d_(data), p_(p), head_(1u << 15, -1), prev_(data.size(), -1) {}

    int hashAt(std::size_t i) const {
        const std::uint32_t v = static_cast<std::uint8_t>(d_[i]) | (static_cast<std::uint8_t>(d_[i + 1]) << 8) |
                                (static_cast<std::uint8_t>(d_[i + 2]) << 16);
        return static_cast<int>((v * 2654435761u) >> 17);
    }
    void insert(std::size_t i) {
        if (i + kMinMatch > d_.size()) return;
        const int h = hashAt(i);
        prev_[i] = head_[static_cast<std::size_t>(h)];
        head_[static_cast<std::size_t>(h)] = static_cast<int>(i);
    }
    // Longest match at i (length, distance); length 0 when none of at least kMinMatch.
    std::pair<int, int> find(std::size_t i) const {
        if (i + kMinMatch > d_.size()) return {0, 0};
        const std::size_t maxLen = std::min<std::size_t>(kMaxMatch, d_.size() - i);
        int best = 0, bestDist = 0, chain = p_.chain;
        int cand = head_[static_cast<std::size_t>(hashAt(i))];
        while (cand >= 0 && chain-- > 0) {
            const std::size_t dist = i - static_cast<std::size_t>(cand);
            if (dist > static_cast<std::size_t>(kWindow)) break;
            const char* a = d_.data() + i;
            const char* b = d_.data() + cand;
            std::size_t l = 0;
            if (best == 0 || a[best] == b[best] || static_cast<std::size_t>(best) >= maxLen) {
                while (l < maxLen && a[l] == b[l]) l++;
            }
            if (static_cast<int>(l) > best) {
                best = static_cast<int>(l);
                bestDist = static_cast<int>(dist);
                if (best >= p_.nice || static_cast<std::size_t>(best) >= maxLen) break;
            }
            cand = prev_[static_cast<std::size_t>(cand)];
        }
        if (best < kMinMatch) return {0, 0};
        // A length-3 match from far away costs more than three literals.
        if (best == kMinMatch && bestDist > 4096) return {0, 0};
        return {best, bestDist};
    }

private:
    std::string_view d_;
    LevelParams p_;
    std::vector<int> head_;
    std::vector<int> prev_;
};

std::vector<Token> tokenize(std::string_view data, int level) {
    std::vector<Token> tokens;
    tokens.reserve(data.size() / 2 + 16);
    const LevelParams p = paramsFor(level);
    Matcher m(data, p);
    std::size_t i = 0;
    while (i < data.size()) {
        auto [len, dist] = m.find(i);
        if (len >= kMinMatch && p.lazy && i + 1 < data.size() && len < p.nice) {
            // Lazy matching: take a literal now if the next position matches better.
            m.insert(i);
            const auto next = m.find(i + 1);
            if (next.first > len + 1) {
                tokens.push_back({static_cast<std::uint8_t>(data[i]), 0});
                i++;
                continue;
            }
            // keep the current match; position i is already inserted
            tokens.push_back({static_cast<std::uint16_t>(len), static_cast<std::uint16_t>(dist)});
            for (int k = 1; k < len; k++) m.insert(i + static_cast<std::size_t>(k));
            i += static_cast<std::size_t>(len);
            continue;
        }
        if (len >= kMinMatch) {
            tokens.push_back({static_cast<std::uint16_t>(len), static_cast<std::uint16_t>(dist)});
            for (int k = 0; k < len; k++) m.insert(i + static_cast<std::size_t>(k));
            i += static_cast<std::size_t>(len);
        } else {
            tokens.push_back({static_cast<std::uint8_t>(data[i]), 0});
            m.insert(i);
            i++;
        }
    }
    return tokens;
}

// -------------------------------------------------------------- block coder

// Code length tables are sent as run-length tokens, each token a fixed 5-bit symbol:
// 0..15 a length, 16 repeat previous 3..6 times (+2 bits), 17 zeros 3..10 (+3), 18 zeros 11..138 (+7).
void writeLengths(BitWriter& bw, const std::vector<std::uint8_t>& lens) {
    std::size_t i = 0;
    int prev = -1;
    while (i < lens.size()) {
        const int cur = lens[i];
        std::size_t run = 1;
        while (i + run < lens.size() && lens[i + run] == cur) run++;
        if (cur == 0 && run >= 3) {
            const std::size_t r = std::min<std::size_t>(run, 138);
            if (r >= 11) {
                bw.put(18, 5);
                bw.put(static_cast<std::uint32_t>(r - 11), 7);
            } else {
                bw.put(17, 5);
                bw.put(static_cast<std::uint32_t>(r - 3), 3);
            }
            i += r;
            prev = 0;
        } else if (cur == prev && run >= 3) {
            const std::size_t r = std::min<std::size_t>(run, 6);
            bw.put(16, 5);
            bw.put(static_cast<std::uint32_t>(r - 3), 2);
            i += r;
        } else {
            bw.put(static_cast<std::uint32_t>(cur), 5);
            prev = cur;
            i++;
        }
    }
}

bool readLengths(BitReader& br, std::vector<std::uint8_t>& lens, std::size_t total) {
    lens.clear();
    int prev = -1;
    while (lens.size() < total) {
        const std::uint32_t t = br.get(5);
        if (t <= 15) {
            lens.push_back(static_cast<std::uint8_t>(t));
            prev = static_cast<int>(t);
        } else if (t == 16) {
            if (prev < 0) return false;
            const std::size_t r = 3 + br.get(2);
            for (std::size_t k = 0; k < r; k++) lens.push_back(static_cast<std::uint8_t>(prev));
        } else if (t == 17 || t == 18) {
            const std::size_t r = t == 17 ? 3 + br.get(3) : 11 + br.get(7);
            for (std::size_t k = 0; k < r; k++) lens.push_back(0);
            prev = 0;
        } else {
            return false;
        }
        if (lens.size() > total) return false;
    }
    return true;
}

// payload of mode 1 (and of the inner coder of modes 2 and 3).
std::string encodeBlock(std::string_view data, int level) {
    const std::vector<Token> tokens = tokenize(data, level);
    std::vector<std::uint32_t> litFreq(kLitLen, 0), distFreq(kDist, 0);
    for (const Token& t : tokens) {
        if (t.dist == 0) {
            litFreq[t.lenOrLit]++;
        } else {
            litFreq[static_cast<std::size_t>(257 + lengthCode(t.lenOrLit))]++;
            distFreq[static_cast<std::size_t>(distCode(t.dist))]++;
        }
    }
    litFreq[kEnd]++;
    std::vector<std::uint8_t> litLens = buildLengths(litFreq);
    std::vector<std::uint8_t> distLens = buildLengths(distFreq);
    const auto litCodes = buildCodes(litLens);
    const auto distCodes = buildCodes(distLens);

    BitWriter bw;
    std::vector<std::uint8_t> all = litLens;
    all.insert(all.end(), distLens.begin(), distLens.end());
    writeLengths(bw, all);
    for (const Token& t : tokens) {
        if (t.dist == 0) {
            bw.put(litCodes[t.lenOrLit], litLens[t.lenOrLit]);
        } else {
            const int lc = lengthCode(t.lenOrLit);
            const std::size_t sym = static_cast<std::size_t>(257 + lc);
            bw.put(litCodes[sym], litLens[sym]);
            if (kLenExtra[static_cast<std::size_t>(lc)]) bw.put(static_cast<std::uint32_t>(t.lenOrLit - kLenBase[static_cast<std::size_t>(lc)]), kLenExtra[static_cast<std::size_t>(lc)]);
            const int dc = distCode(t.dist);
            bw.put(distCodes[static_cast<std::size_t>(dc)], distLens[static_cast<std::size_t>(dc)]);
            if (kDistExtra[static_cast<std::size_t>(dc)]) bw.put(static_cast<std::uint32_t>(t.dist - kDistBase[static_cast<std::size_t>(dc)]), kDistExtra[static_cast<std::size_t>(dc)]);
        }
    }
    bw.put(litCodes[kEnd], litLens[kEnd]);
    return bw.finish();
}

std::optional<std::string> decodeBlock(std::string_view payload, std::size_t expected) {
    BitReader br(payload);
    std::vector<std::uint8_t> all;
    if (!readLengths(br, all, kLitLen + kDist)) return std::nullopt;
    const std::vector<std::uint8_t> litLens(all.begin(), all.begin() + kLitLen);
    const std::vector<std::uint8_t> distLens(all.begin() + kLitLen, all.end());
    Decoder lit, dist;
    if (!lit.build(litLens) || !dist.build(distLens) || lit.empty) return std::nullopt;
    std::string out;
    out.reserve(expected);
    for (;;) {
        br.refill();
        const int sym = lit.decode(br);
        if (sym < 0) return std::nullopt;
        if (sym < 256) {
            if (out.size() >= expected) return std::nullopt;
            out.push_back(static_cast<char>(sym));
        } else if (sym == kEnd) {
            break;
        } else {
            const std::size_t lc = static_cast<std::size_t>(sym - 257);
            if (lc >= kLenBase.size()) return std::nullopt;
            int len = kLenBase[lc];
            if (kLenExtra[lc]) len += static_cast<int>(br.get(kLenExtra[lc]));
            br.refill();
            const int ds = dist.empty ? -1 : dist.decode(br);
            if (ds < 0 || ds >= kDist) return std::nullopt;
            int d = kDistBase[static_cast<std::size_t>(ds)];
            if (kDistExtra[static_cast<std::size_t>(ds)]) d += static_cast<int>(br.get(kDistExtra[static_cast<std::size_t>(ds)]));
            if (static_cast<std::size_t>(d) > out.size() || out.size() + static_cast<std::size_t>(len) > expected) return std::nullopt;
            std::size_t from = out.size() - static_cast<std::size_t>(d);
            for (int k = 0; k < len; k++) out.push_back(out[from + static_cast<std::size_t>(k)]);
        }
        if (br.exhausted()) return std::nullopt;
    }
    if (out.size() != expected) return std::nullopt;
    return out;
}

// ------------------------------------------------------- numeric transforms

bool isDigit(char c) { return c >= '0' && c <= '9'; }

struct Tokens {
    char sep = 0;
    bool trailing = false;
    std::vector<std::string_view> items;
};

// Splits "a,b,c" (optionally ending with the separator). Needs >= 2 items or a trailing separator.
bool splitList(std::string_view s, Tokens& t) {
    if (s.empty()) return false;
    // the separator is the first character that cannot be part of a number
    char sep = 0;
    for (char c : s) {
        if (!isDigit(c) && c != '-' && c != '.') { sep = c; break; }
    }
    if (sep == 0) {
        t.sep = ',';  // a single number: no separator in the text
        t.trailing = false;
        t.items.assign(1, s);
        return true;
    }
    t.sep = sep;
    std::size_t start = 0;
    t.items.clear();
    for (std::size_t i = 0; i <= s.size(); i++) {
        if (i == s.size() || s[i] == sep) {
            if (i == s.size() && start == s.size()) { t.trailing = true; break; }
            t.items.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    t.trailing = s.back() == sep;
    return !t.items.empty();
}

// Canonical integer: -?(0|[1-9][0-9]{0,17}); "-0" is not canonical.
bool parseInt(std::string_view s, std::int64_t& out) {
    std::size_t i = 0;
    bool neg = false;
    if (!s.empty() && s[0] == '-') { neg = true; i = 1; }
    const std::size_t digits = s.size() - i;
    if (digits == 0 || digits > 18) return false;
    if (s[i] == '0' && digits > 1) return false;
    std::int64_t v = 0;
    for (; i < s.size(); i++) {
        if (!isDigit(s[i])) return false;
        v = v * 10 + (s[i] - '0');
    }
    if (neg && v == 0) return false;
    out = neg ? -v : v;
    return true;
}

// Canonical decimal: integer part canonical, optional ".digits" (1..17 digits), <= 18 digits in all.
bool parseDecimal(std::string_view s, std::int64_t& mantissa, int& frac) {
    const std::size_t dot = s.find('.');
    if (dot == std::string_view::npos) {
        frac = 0;
        return parseInt(s, mantissa);
    }
    const std::string_view ip = s.substr(0, dot), fp = s.substr(dot + 1);
    if (fp.empty() || fp.size() > 17) return false;
    std::int64_t i;
    // the integer part may be "-0" here (e.g. -0.5); treat sign separately
    bool neg = !ip.empty() && ip[0] == '-';
    std::string_view mag = neg ? ip.substr(1) : ip;
    if (mag.empty() || mag.size() + fp.size() > 18) return false;
    if (mag[0] == '0' && mag.size() > 1) return false;
    for (char c : mag) if (!isDigit(c)) return false;
    for (char c : fp) if (!isDigit(c)) return false;
    (void)i;
    std::int64_t v = 0;
    for (char c : mag) v = v * 10 + (c - '0');
    for (char c : fp) v = v * 10 + (c - '0');
    if (neg && v == 0) return false;
    mantissa = neg ? -v : v;
    frac = static_cast<int>(fp.size());
    return true;
}

std::string formatDecimal(std::int64_t mantissa, int frac) {
    const bool neg = mantissa < 0;
    std::string digits = std::to_string(neg ? -mantissa : mantissa);
    if (frac > 0) {
        if (static_cast<int>(digits.size()) <= frac) digits.insert(0, static_cast<std::size_t>(frac) + 1 - digits.size(), '0');
        digits.insert(digits.size() - static_cast<std::size_t>(frac), ".");
    }
    return neg ? "-" + digits : digits;
}

// Mode 2 transform bytes, or empty when the data is not an integer list.
std::string transformInts(std::string_view s) {
    Tokens t;
    if (!splitList(s, t)) return {};
    std::string out;
    out.push_back(t.sep);
    out.push_back(t.trailing ? 1 : 0);
    putVarint(out, t.items.size());
    std::int64_t prev = 0;
    for (std::string_view item : t.items) {
        std::int64_t v;
        if (!parseInt(item, v)) return {};
        putVarint(out, zigzag(v - prev));
        prev = v;
    }
    return out;
}

std::optional<std::string> untransformInts(std::string_view b) {
    if (b.size() < 3) return std::nullopt;
    const char sep = b[0];
    const bool trailing = b[1] != 0;
    std::size_t pos = 2;
    std::uint64_t count;
    if (!getVarint(b, pos, count) || count == 0 || count > kMaxOriginal) return std::nullopt;
    std::string out;
    std::int64_t prev = 0;
    for (std::uint64_t i = 0; i < count; i++) {
        std::uint64_t z;
        if (!getVarint(b, pos, z)) return std::nullopt;
        prev += unzigzag(z);
        if (i) out.push_back(sep);
        out += std::to_string(prev);
        if (out.size() > kMaxOriginal) return std::nullopt;
    }
    if (trailing) out.push_back(sep);
    if (pos != b.size()) return std::nullopt;
    return out;
}

std::string transformDecimals(std::string_view s) {
    Tokens t;
    if (!splitList(s, t)) return {};
    std::string head;
    head.push_back(t.sep);
    head.push_back(t.trailing ? 1 : 0);
    putVarint(head, t.items.size());
    std::string fracs, mants;
    for (std::string_view item : t.items) {
        std::int64_t m;
        int f;
        if (!parseDecimal(item, m, f)) return {};
        fracs.push_back(static_cast<char>(f));
        putVarint(mants, zigzag(m));
    }
    return head + fracs + mants;
}

std::optional<std::string> untransformDecimals(std::string_view b) {
    if (b.size() < 3) return std::nullopt;
    const char sep = b[0];
    const bool trailing = b[1] != 0;
    std::size_t pos = 2;
    std::uint64_t count;
    if (!getVarint(b, pos, count) || count == 0 || count > kMaxOriginal || pos + count > b.size()) return std::nullopt;
    const std::string_view fracs = b.substr(pos, count);
    pos += count;
    std::string out;
    for (std::uint64_t i = 0; i < count; i++) {
        std::uint64_t z;
        if (!getVarint(b, pos, z)) return std::nullopt;
        const int f = static_cast<unsigned char>(fracs[i]);
        if (f > 17) return std::nullopt;
        if (i) out.push_back(sep);
        out += formatDecimal(unzigzag(z), f);
        if (out.size() > kMaxOriginal) return std::nullopt;
    }
    if (trailing) out.push_back(sep);
    if (pos != b.size()) return std::nullopt;
    return out;
}

std::string container(int mode, std::string_view original, std::uint32_t crc, const std::string& payload) {
    std::string out = "PKN";
    out.push_back(1);
    out.push_back(static_cast<char>(mode));
    putVarint(out, original.size());
    for (int i = 0; i < 4; i++) out.push_back(static_cast<char>((crc >> (8 * i)) & 0xff));
    out += payload;
    return out;
}

}  // namespace

std::uint32_t crc32(std::string_view data) {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; i++) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    std::uint32_t c = 0xFFFFFFFFu;
    for (char ch : data) c = table[(c ^ static_cast<std::uint8_t>(ch)) & 0xff] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

std::string compress(std::string_view data, int level) {
    level = std::clamp(level, kMinLevel, kMaxLevel);
    const std::uint32_t crc = crc32(data);
    std::string best = container(0, data, crc, std::string(data));
    auto consider = [&](int mode, const std::string& payload) {
        std::string c = container(mode, data, crc, payload);
        if (c.size() < best.size()) best = std::move(c);
    };
    if (data.empty()) return best;
    consider(1, encodeBlock(data, level));
    for (int mode : {2, 3}) {
        const std::string t = mode == 2 ? transformInts(data) : transformDecimals(data);
        if (t.empty()) continue;
        std::string payload;
        putVarint(payload, t.size());
        payload += encodeBlock(t, level);
        consider(mode, payload);
    }
    return best;
}

std::optional<Header> readHeader(std::string_view p) {
    if (p.size() < 5 + 1 + 4 || p.substr(0, 3) != "PKN" || p[3] != 1) return std::nullopt;
    Header h;
    h.mode = static_cast<unsigned char>(p[4]);
    if (h.mode > 3) return std::nullopt;
    std::size_t pos = 5;
    if (!getVarint(p, pos, h.originalLength) || h.originalLength > kMaxOriginal) return std::nullopt;
    if (pos + 4 > p.size()) return std::nullopt;
    h.crc32 = 0;
    for (int i = 0; i < 4; i++) h.crc32 |= static_cast<std::uint32_t>(static_cast<unsigned char>(p[pos + static_cast<std::size_t>(i)])) << (8 * i);
    pos += 4;
    h.payloadOffset = pos;
    const std::size_t payloadSize = p.size() - pos;
    if (h.mode == 0 && payloadSize != h.originalLength) return std::nullopt;
    if (h.mode != 0 && payloadSize == 0) return std::nullopt;
    if (h.originalLength == 0 && h.mode != 0) return std::nullopt;
    return h;
}

std::optional<std::string> decompress(std::string_view packed) {
    const auto header = readHeader(packed);
    if (!header) return std::nullopt;
    const std::string_view payload = packed.substr(header->payloadOffset);
    std::optional<std::string> out;
    if (header->mode == 0) {
        out = std::string(payload);
    } else if (header->mode == 1) {
        out = decodeBlock(payload, static_cast<std::size_t>(header->originalLength));
    } else {
        std::size_t pos = 0;
        std::uint64_t tlen;
        if (!getVarint(payload, pos, tlen) || tlen > kMaxOriginal * 4) return std::nullopt;
        const auto bytes = decodeBlock(payload.substr(pos), static_cast<std::size_t>(tlen));
        if (!bytes) return std::nullopt;
        out = header->mode == 2 ? untransformInts(*bytes) : untransformDecimals(*bytes);
    }
    if (!out || out->size() != header->originalLength || crc32(*out) != header->crc32) return std::nullopt;
    return out;
}

std::string toByteString(std::string_view bytes) {
    std::string out;
    out.reserve(bytes.size() + bytes.size() / 4);
    for (unsigned char c : bytes) {
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3f)));
        }
    }
    return out;
}

std::optional<std::string> fromByteString(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
            i++;
        } else if ((c & 0xFE) == 0xC2 && i + 1 < s.size() && (static_cast<unsigned char>(s[i + 1]) & 0xC0) == 0x80) {
            out.push_back(static_cast<char>(((c & 0x03) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3f)));
            i += 2;
        } else {
            return std::nullopt;
        }
    }
    return out;
}

}  // namespace lithium::packin

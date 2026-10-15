// Packin: Lithium's general-purpose compressor (used by the Packin extension).
//
// Container (all integers little-endian, varints are LEB128):
//   "PKN"  version(1)=1  mode(1)  originalLength(varint)  crc32(original)(4)  payload
// Modes:
//   0  stored          payload = the original bytes
//   1  lz+huffman      payload = LZ77 tokens, one canonical Huffman block
//   2  integer list    original is "n1<sep>n2<sep>..." of canonical integers: payload =
//                      sep, count, zigzag-delta varints, run through the mode-1 coder
//   3  decimal list    same shape with decimal numbers: per-token fraction-digit counts and
//                      scaled mantissas, run through the mode-1 coder
// compress() tries every mode that applies and keeps the smallest result, so text goes to
// mode 1 and numeric lists to modes 2 or 3 without the caller choosing.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace lithium::packin {

constexpr int kMinLevel = 1;
constexpr int kMaxLevel = 9;
constexpr int kDefaultLevel = 6;

// Compresses arbitrary bytes. `level` (1 fastest .. 9 smallest) is clamped.
std::string compress(std::string_view data, int level = kDefaultLevel);

// Recovers the original bytes; nullopt when the data is not valid Packin data or is corrupt
// (bad header, truncated payload, checksum mismatch).
std::optional<std::string> decompress(std::string_view packed);

struct Header {
    int mode = 0;
    std::uint64_t originalLength = 0;
    std::uint32_t crc32 = 0;
    std::size_t payloadOffset = 0;
};
// Validates the header and the structural facts that can be checked without decoding
// (magic, version, mode, length fields, a stored payload of exactly the right size).
std::optional<Header> readHeader(std::string_view packed);

std::uint32_t crc32(std::string_view data);

// Scratch strings carry bytes as characters U+0000..U+00FF. These convert between that
// text form (UTF-8 encoded here) and raw bytes. fromByteString fails if a character is
// above U+00FF.
std::string toByteString(std::string_view bytes);
std::optional<std::string> fromByteString(std::string_view utf8);

}  // namespace lithium::packin

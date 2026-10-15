// OutList list files: naming rules, the on-disk format and list fingerprints.
//
// File format (version 1; integers are LEB128 varints, crc32 little-endian):
//   "OUTL"  version(1)=1  flags(1)=0
//   itemCount  chunkItems  chunkCount
//   per chunk: kind(1)  itemsInChunk  byteLength
//   crc32 of everything above
//   chunk payloads back to back; each is one Packin container
// Chunk kinds: 0 = tagged items (0 string: varint length + UTF-8, 1 number: 8-byte IEEE
// double, 2 true, 3 false); 1 = numbers only, written as decimal text separated by '\n'
// so Packin's numeric modes apply.
// The header gives the item count and the position of every chunk without reading any
// payload, which is what later versions need for existence checks, ranges and appends.
// This version still loads and saves whole lists.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lithium/value.h"

namespace lithium::outlist {

constexpr std::size_t kMaxNameBytes = 100;
constexpr std::size_t kMaxItems = 200000;  // same limit as Scratch lists
constexpr std::size_t kChunkItems = 4096;

// Empty string when valid, otherwise the reason.
std::string validateName(std::string_view name);

std::string encode(const std::vector<Value>& items);
// nullopt with `error` set when the file is not a valid, intact OutList file.
std::optional<std::vector<Value>> decode(std::string_view file, std::string& error);

struct Info {
    std::uint64_t itemCount = 0;
    std::size_t chunks = 0;
};
// Header only: validates the header checksum and chunk table, reads no payload.
std::optional<Info> readInfo(std::string_view file, std::string& error);

// Content hash used to tell whether a list changed since it was saved or loaded.
std::uint64_t fingerprint(const std::vector<Value>& items);

}  // namespace lithium::outlist

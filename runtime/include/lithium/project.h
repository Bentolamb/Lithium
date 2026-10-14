// In-memory project model.
//
// Shaped like the Scratch 3 (SB3) block graph so the SB3 loader (a later
// milestone) can map onto it directly: blocks have an opcode, a next/parent
// link, named inputs (literal or nested block) and named fields. Blocks live
// in a vector and refer to each other by index; the SB3 string id is kept for
// export and debugging.
#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "lithium/value.h"

namespace lithium {

struct Primitive;  // runtime-internal: resolved block implementation

using BlockIndex = std::int32_t;
inline constexpr BlockIndex kNoBlock = -1;

struct Input {
    enum class Kind : std::uint8_t {
        None,     // input not connected
        Literal,  // typed-in value (an SB3 shadow primitive)
        Block     // reporter, or first block of a statement branch
    };
    Kind kind = Kind::None;
    Value literal;
    BlockIndex block = kNoBlock;
};

struct Field {
    Value value;
    std::string id;  // e.g. the variable id for VARIABLE fields
};

struct Block {
    std::string id;
    std::string opcode;
    BlockIndex next = kNoBlock;
    BlockIndex parent = kNoBlock;
    bool topLevel = false;
    std::vector<std::pair<std::string, Input>> inputs;
    std::vector<std::pair<std::string, Field>> fields;

    // Runtime cache of the resolved primitive; not part of the project data.
    mutable const Primitive* primitive = nullptr;
    mutable bool primitiveResolved = false;

    const Input* findInput(std::string_view name) const;
    const Field* findField(std::string_view name) const;
};

class BlockContainer {
public:
    // Creates a block and returns its index. An empty id gets a generated one.
    BlockIndex create(std::string opcode, std::string id = {});

    bool valid(BlockIndex index) const {
        return index >= 0 && static_cast<std::size_t>(index) < blocks_.size();
    }
    const Block& at(BlockIndex index) const { return blocks_[static_cast<std::size_t>(index)]; }
    Block& at(BlockIndex index) { return blocks_[static_cast<std::size_t>(index)]; }
    std::size_t size() const { return blocks_.size(); }
    BlockIndex find(std::string_view id) const;  // kNoBlock if missing

    void setNext(BlockIndex block, BlockIndex next);
    void setInputLiteral(BlockIndex block, std::string name, Value value);
    void setInputBlock(BlockIndex block, std::string name, BlockIndex reporter);
    // Statement input (SUBSTACK, SUBSTACK2). kNoBlock leaves the branch empty.
    void setBranch(BlockIndex block, std::string name, BlockIndex first);
    void setField(BlockIndex block, std::string name, Value value, std::string id = {});
    void setTopLevel(BlockIndex block, bool topLevel = true);

    BlockIndex getNextBlock(BlockIndex block) const;
    // branchNum 1 -> SUBSTACK, 2 -> SUBSTACK2. kNoBlock when empty.
    BlockIndex getBranch(BlockIndex block, int branchNum) const;

    // Top-level blocks in creation order (Scratch keeps the same order).
    const std::vector<BlockIndex>& scripts() const { return scripts_; }

private:
    void setInput(BlockIndex block, std::string name, Input input);

    std::vector<Block> blocks_;
    std::vector<BlockIndex> scripts_;
    std::unordered_map<std::string, BlockIndex> byId_;
};

struct Variable {
    std::string id;
    std::string name;
    Value value;
};

class Target {
public:
    Target(std::string name, bool isStage) : name(std::move(name)), isStage(isStage) {}

    std::string name;
    bool isStage;
    bool isOriginal = true;  // false for clones (not implemented yet)
    double x = 0.0;
    double y = 0.0;
    double direction = 90.0;
    double size = 100.0;
    bool visible = true;
    BlockContainer blocks;
    std::deque<Variable> variables;  // deque: references stay valid on growth

    Variable& addVariable(std::string id, std::string name, Value initial);
    // Local variables first, then the stage's (globals). nullptr if missing.
    Variable* lookupVariableById(std::string_view id);
    Variable* lookupVariableByName(std::string_view name);
    // Mirrors Target.lookupOrCreateVariable: by id, then by name, else create
    // a local variable.
    Variable& lookupOrCreateVariable(std::string_view id, std::string_view name);

    void setStage(Target* stage) { stage_ = stage; }

private:
    Target* stage_ = nullptr;
};

class Project {
public:
    Project();

    std::string name = "Project";

    Target& stage() { return *targets_.front(); }
    // Sprites are added on top of earlier ones (higher layer).
    Target& addSprite(std::string name);
    Target* findTarget(std::string_view name);

    // Executable order: stage first, then sprites from bottom layer to top.
    const std::vector<std::unique_ptr<Target>>& targets() const { return targets_; }

private:
    std::vector<std::unique_ptr<Target>> targets_;
};

}  // namespace lithium

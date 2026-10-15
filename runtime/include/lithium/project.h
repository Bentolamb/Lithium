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
#include <optional>
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
    // Cast.toNumber(literal), computed on first use (literals are usually numeric strings).
    mutable double literalNumber = 0.0;
    mutable bool literalNumberSet = false;
    std::string id;  // broadcast message id for [11, name, id] primitives
    BlockIndex block = kNoBlock;
};

struct Variable;
struct Field {
    Value value;
    std::string id;  // e.g. the variable id for VARIABLE fields
    // Last variable this field resolved to, for the target with this uid (a lookup cache).
    mutable int cachedTargetUid = -1;
    mutable Variable* cachedVariable = nullptr;
};

// <mutation> of procedures_prototype / procedures_call blocks.
struct Mutation {
    std::string proccode;
    std::vector<std::string> argumentIds;
    std::vector<std::string> argumentNames;
    std::vector<Value> argumentDefaults;
    bool warp = false;
};

struct Block {
    std::string id;
    std::string opcode;
    BlockIndex next = kNoBlock;
    BlockIndex parent = kNoBlock;
    bool topLevel = false;
    std::vector<std::pair<std::string, Input>> inputs;
    std::vector<std::pair<std::string, Field>> fields;
    std::optional<Mutation> mutation;

    // Runtime cache of the resolved primitive; not part of the project data.
    mutable const Primitive* primitive = nullptr;
    mutable bool primitiveResolved = false;
    // Loop blocks only: -1 unknown, 0 body needs the scheduler, 1 body is a plain command chain.
    mutable std::int8_t fusableBody = -1;

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
    void setInputLiteral(BlockIndex block, std::string name, Value value, std::string id = {});
    void setInputBlock(BlockIndex block, std::string name, BlockIndex reporter);
    // Statement input (SUBSTACK, SUBSTACK2). kNoBlock leaves the branch empty.
    void setBranch(BlockIndex block, std::string name, BlockIndex first);
    void setField(BlockIndex block, std::string name, Value value, std::string id = {});
    void setTopLevel(BlockIndex block, bool topLevel = true);
    void setMutation(BlockIndex block, Mutation mutation);

    // Custom blocks. The definition hat (procedures_definition) whose
    // prototype has this proccode, and the first prototype with it. The first
    // match in block order wins, as in scratch-vm. kNoBlock / nullptr if none.
    BlockIndex procedureDefinition(const std::string& proccode) const;
    const Mutation* procedurePrototype(const std::string& proccode) const;

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
    mutable std::unordered_map<std::string, BlockIndex> definitionCache_;
    mutable std::unordered_map<std::string, BlockIndex> prototypeCache_;
};

struct Variable {
    enum class Type { Scalar, List, Broadcast };
    std::string id;
    std::string name;
    Type type = Type::Scalar;
    Value value;               // Scalar
    std::vector<Value> list;   // List
};

// Asset metadata. The pixels and audio live in the host; the runtime only
// needs names and the ids to refer to them.
struct Costume {
    std::string name;
    std::string assetId;
    std::string md5ext;
    std::string dataFormat;
    double rotationCenterX = 0.0;
    double rotationCenterY = 0.0;
    double bitmapResolution = 1.0;
    int skinId = -1;  // renderer skin, assigned by the host after loading
};

struct Sound {
    std::string name;
    std::string assetId;
    std::string md5ext;
    std::string dataFormat;
    double rate = 0.0;
    double sampleCount = 0.0;
};

// What clones of a sprite share.
struct SpriteAssets {
    std::vector<Costume> costumes;
    std::vector<Sound> sounds;
};

enum class RotationStyle { AllAround, LeftRight, DontRotate };

struct GraphicEffects {
    double color = 0, fisheye = 0, whirl = 0, pixelate = 0, mosaic = 0, brightness = 0, ghost = 0;
    // nullptr for names that are not graphic effects.
    double* byName(std::string_view name) {
        if (name == "color") return &color;
        if (name == "fisheye") return &fisheye;
        if (name == "whirl") return &whirl;
        if (name == "pixelate") return &pixelate;
        if (name == "mosaic") return &mosaic;
        if (name == "brightness") return &brightness;
        if (name == "ghost") return &ghost;
        return nullptr;
    }
    void clear() { *this = GraphicEffects{}; }
};

struct PenState {
    bool down = false;
    double color = 66.66, saturation = 100, brightness = 100, transparency = 0;
    double shade = 50;  // legacy "change shade" blocks only
    double color4f[4] = {0, 0, 1, 1};
    double diameter = 1;
};

struct Bubble {
    std::string type = "say";
    std::string text;
    std::uint64_t usageId = 0;
};

class Target {
public:
    Target(std::string name, bool isStage)
        : name(std::move(name)), isStage(isStage), blocksStore_(std::make_shared<BlockContainer>()), blocks(*blocksStore_) {}
    // Clone constructor: shares the block graph of the original.
    explicit Target(const Target& original)
        : name(original.name), isStage(false), isOriginal(false), x(original.x), y(original.y),
          direction(original.direction), size(original.size), visible(original.visible),
          blocksStore_(original.blocksStore_), blocks(*blocksStore_), variables(original.variables),
          assets(original.assets), currentCostume(original.currentCostume), rotationStyle(original.rotationStyle),
          draggable(original.draggable), effects(original.effects), soundPitch(original.soundPitch),
          soundPan(original.soundPan), pen(original.pen), edgeValues(original.edgeValues), stage_(original.stage_) {}
    Target& operator=(const Target&) = delete;

    std::string name;
    bool isStage;
    bool isOriginal = true;  // false for clones
    std::uint64_t cloneSerial = 0;  // creation order of clones
    double x = 0.0;
    double y = 0.0;
    double direction = 90.0;
    double size = 100.0;
    bool visible = true;
    std::shared_ptr<BlockContainer> blocksStore_;
    BlockContainer& blocks;
    std::deque<Variable> variables;  // deque: references stay valid on growth

    // ---- looks, sound, pen -------------------------------------------------
    std::shared_ptr<SpriteAssets> assets = std::make_shared<SpriteAssets>();
    int currentCostume = 0;
    RotationStyle rotationStyle = RotationStyle::AllAround;
    bool draggable = false;
    GraphicEffects effects;
    double volume = 100.0;
    double soundPitch = 0.0, soundPan = 0.0;
    int drawableId = -1;          // host renderer drawable
    int uid = 0;                  // stable id for host notifications
    PenState pen;
    Bubble bubble;
    double tempo = 60.0;          // stage only
    std::unordered_map<BlockIndex, bool> edgeValues;  // edge-activated hat predicates
    const std::vector<Costume>& costumes() const { return assets->costumes; }
    std::vector<Costume>& costumes() { return assets->costumes; }
    int costumeIndexByName(std::string_view name) const;

    Variable& addVariable(std::string id, std::string name, Value initial);
    Variable& addList(std::string id, std::string name, std::vector<Value> items);
    Variable& addBroadcast(std::string id, std::string name);
    // Local variables first, then the stage's (globals). nullptr if missing.
    // By id the type does not matter (as in scratch-vm).
    Variable* lookupVariableById(std::string_view id);
    Variable* lookupVariableByNameAndType(std::string_view name, Variable::Type type);
    // Mirror Target.lookupOrCreateVariable / lookupOrCreateList: by id, then
    // by name and type, else create a local variable.
    Variable& lookupOrCreateVariable(std::string_view id, std::string_view name);
    Variable& lookupOrCreateList(std::string_view id, std::string_view name);
    // Target.lookupBroadcastMsg: by id when given, else by name (case-insensitive).
    // Call on the stage. nullptr if there is no such message.
    Variable* lookupBroadcastMsg(std::string_view id, std::string_view name);

    void setStage(Target* stage) { stage_ = stage; }
    Target* stage() const { return stage_; }

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
    // Clone support: place `clone` just before `behind` in execution order
    // (Target.goBehindOther); remove a target and hand ownership back.
    Target& insertBefore(std::unique_ptr<Target> clone, const Target* behind);
    std::unique_ptr<Target> remove(const Target* target);

    // Executable order: stage first, then sprites from bottom layer to top.
    const std::vector<std::unique_ptr<Target>>& targets() const { return targets_; }
    std::vector<std::unique_ptr<Target>>& mutableTargets() { return targets_; }

private:
    std::vector<std::unique_ptr<Target>> targets_;
};

}  // namespace lithium

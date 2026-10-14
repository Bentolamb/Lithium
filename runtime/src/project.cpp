#include "lithium/project.h"
#include "lithium/text.h"

namespace lithium {

const Input* Block::findInput(std::string_view name) const {
    for (const auto& entry : inputs) {
        if (entry.first == name) return &entry.second;
    }
    return nullptr;
}

const Field* Block::findField(std::string_view name) const {
    for (const auto& entry : fields) {
        if (entry.first == name) return &entry.second;
    }
    return nullptr;
}

BlockIndex BlockContainer::create(std::string opcode, std::string id) {
    const BlockIndex index = static_cast<BlockIndex>(blocks_.size());
    if (id.empty()) id = "block" + std::to_string(index);
    Block block;
    block.id = id;
    block.opcode = std::move(opcode);
    byId_[id] = index;
    definitionCache_.clear();
    prototypeCache_.clear();
    blocks_.push_back(std::move(block));
    return index;
}

BlockIndex BlockContainer::find(std::string_view id) const {
    const auto it = byId_.find(std::string(id));
    return it == byId_.end() ? kNoBlock : it->second;
}

void BlockContainer::setNext(BlockIndex block, BlockIndex next) {
    at(block).next = next;
    if (valid(next)) at(next).parent = block;
}

void BlockContainer::setInput(BlockIndex block, std::string name, Input input) {
    auto& inputs = at(block).inputs;
    for (auto& entry : inputs) {
        if (entry.first == name) {
            entry.second = std::move(input);
            return;
        }
    }
    inputs.emplace_back(std::move(name), std::move(input));
}

void BlockContainer::setInputLiteral(BlockIndex block, std::string name, Value value, std::string id) {
    Input input;
    input.kind = Input::Kind::Literal;
    input.literal = std::move(value);
    input.id = std::move(id);
    setInput(block, std::move(name), std::move(input));
}

void BlockContainer::setInputBlock(BlockIndex block, std::string name, BlockIndex reporter) {
    Input input;
    input.kind = Input::Kind::Block;
    input.block = reporter;
    if (valid(reporter)) at(reporter).parent = block;
    setInput(block, std::move(name), std::move(input));
}

void BlockContainer::setBranch(BlockIndex block, std::string name, BlockIndex first) {
    Input input;
    if (valid(first)) {
        input.kind = Input::Kind::Block;
        input.block = first;
        at(first).parent = block;
    }
    setInput(block, std::move(name), std::move(input));
}

void BlockContainer::setField(BlockIndex block, std::string name, Value value, std::string id) {
    auto& fields = at(block).fields;
    Field field{std::move(value), std::move(id)};
    for (auto& entry : fields) {
        if (entry.first == name) {
            entry.second = std::move(field);
            return;
        }
    }
    fields.emplace_back(std::move(name), std::move(field));
}

void BlockContainer::setTopLevel(BlockIndex block, bool topLevel) {
    Block& b = at(block);
    if (b.topLevel == topLevel) return;
    b.topLevel = topLevel;
    if (topLevel) {
        scripts_.push_back(block);
    } else {
        for (auto it = scripts_.begin(); it != scripts_.end(); ++it) {
            if (*it == block) {
                scripts_.erase(it);
                break;
            }
        }
    }
}

void BlockContainer::setMutation(BlockIndex block, Mutation mutation) {
    at(block).mutation = std::move(mutation);
    definitionCache_.clear();
    prototypeCache_.clear();
}

const Mutation* BlockContainer::procedurePrototype(const std::string& proccode) const {
    auto it = prototypeCache_.find(proccode);
    if (it == prototypeCache_.end()) {
        BlockIndex found = kNoBlock;
        for (std::size_t i = 0; i < blocks_.size(); i++) {
            const Block& b = blocks_[i];
            if (b.opcode == "procedures_prototype" && b.mutation && b.mutation->proccode == proccode) {
                found = static_cast<BlockIndex>(i);
                break;
            }
        }
        it = prototypeCache_.emplace(proccode, found).first;
    }
    return it->second == kNoBlock ? nullptr : &*blocks_[static_cast<std::size_t>(it->second)].mutation;
}

BlockIndex BlockContainer::procedureDefinition(const std::string& proccode) const {
    auto it = definitionCache_.find(proccode);
    if (it != definitionCache_.end()) return it->second;
    BlockIndex found = kNoBlock;
    for (std::size_t i = 0; i < blocks_.size() && found == kNoBlock; i++) {
        const Block& b = blocks_[i];
        if (b.opcode != "procedures_definition") continue;
        const Input* in = b.findInput("custom_block");
        if (in == nullptr || in->kind != Input::Kind::Block || !valid(in->block)) continue;
        const Block& inner = at(in->block);
        if (inner.mutation && inner.mutation->proccode == proccode) found = static_cast<BlockIndex>(i);
    }
    definitionCache_.emplace(proccode, found);
    return found;
}

BlockIndex BlockContainer::getNextBlock(BlockIndex block) const {
    return valid(block) ? at(block).next : kNoBlock;
}

BlockIndex BlockContainer::getBranch(BlockIndex block, int branchNum) const {
    if (!valid(block)) return kNoBlock;
    const Input* input = branchNum > 2 ? at(block).findInput("SUBSTACK" + std::to_string(branchNum))
                         : at(block).findInput(branchNum == 2 ? std::string_view("SUBSTACK2") : std::string_view("SUBSTACK"));
    if (input == nullptr || input->kind != Input::Kind::Block) return kNoBlock;
    return input->block;
}

int Target::costumeIndexByName(std::string_view n) const {
    for (std::size_t i = 0; i < assets->costumes.size(); i++) {
        if (assets->costumes[i].name == n) return static_cast<int>(i);
    }
    return -1;
}

Variable& Target::addVariable(std::string id, std::string name, Value initial) {
    Variable v;
    v.id = std::move(id);
    v.name = std::move(name);
    v.value = std::move(initial);
    variables.push_back(std::move(v));
    return variables.back();
}

Variable& Target::addList(std::string id, std::string name, std::vector<Value> items) {
    Variable v;
    v.id = std::move(id);
    v.name = std::move(name);
    v.type = Variable::Type::List;
    v.list = std::move(items);
    variables.push_back(std::move(v));
    return variables.back();
}

Variable& Target::addBroadcast(std::string id, std::string name) {
    Variable v;
    v.id = std::move(id);
    v.name = name;
    v.type = Variable::Type::Broadcast;
    v.value = Value::string(std::move(name));
    variables.push_back(std::move(v));
    return variables.back();
}

Variable* Target::lookupVariableById(std::string_view id) {
    for (auto& v : variables) {
        if (v.id == id) return &v;
    }
    if (!isStage && stage_ != nullptr) {
        for (auto& v : stage_->variables) {
            if (v.id == id) return &v;
        }
    }
    return nullptr;
}

Variable* Target::lookupVariableByNameAndType(std::string_view name, Variable::Type type) {
    for (auto& v : variables) {
        if (v.type == type && v.name == name) return &v;
    }
    if (!isStage && stage_ != nullptr) {
        for (auto& v : stage_->variables) {
            if (v.type == type && v.name == name) return &v;
        }
    }
    return nullptr;
}

Variable& Target::lookupOrCreateVariable(std::string_view id, std::string_view name) {
    if (Variable* v = lookupVariableById(id)) return *v;
    if (Variable* v = lookupVariableByNameAndType(name, Variable::Type::Scalar)) return *v;
    return addVariable(std::string(id), std::string(name), Value::number(0.0));
}

Variable& Target::lookupOrCreateList(std::string_view id, std::string_view name) {
    if (Variable* v = lookupVariableById(id)) return *v;
    if (Variable* v = lookupVariableByNameAndType(name, Variable::Type::List)) return *v;
    return addList(std::string(id), std::string(name), {});
}

Variable* Target::lookupBroadcastMsg(std::string_view id, std::string_view name) {
    if (!id.empty()) return lookupVariableById(id);
    if (!name.empty()) {
        const std::string wanted = jsToLowerCase(std::string(name));
        for (auto& v : variables) {
            if (v.type == Variable::Type::Broadcast && jsToLowerCase(v.name) == wanted) return &v;
        }
    }
    return nullptr;
}

Project::Project() {
    targets_.push_back(std::make_unique<Target>("Stage", true));
}

Target& Project::addSprite(std::string name) {
    auto sprite = std::make_unique<Target>(std::move(name), false);
    sprite->setStage(targets_.front().get());
    targets_.push_back(std::move(sprite));
    return *targets_.back();
}

Target& Project::insertBefore(std::unique_ptr<Target> clone, const Target* behind) {
    std::size_t pos = targets_.size();
    for (std::size_t i = 0; i < targets_.size(); i++) {
        if (targets_[i].get() == behind) {
            pos = i;
            break;
        }
    }
    Target& ref = *clone;
    targets_.insert(targets_.begin() + static_cast<std::ptrdiff_t>(pos), std::move(clone));
    return ref;
}

std::unique_ptr<Target> Project::remove(const Target* target) {
    for (std::size_t i = 0; i < targets_.size(); i++) {
        if (targets_[i].get() == target) {
            std::unique_ptr<Target> out = std::move(targets_[i]);
            targets_.erase(targets_.begin() + static_cast<std::ptrdiff_t>(i));
            return out;
        }
    }
    return nullptr;
}

Target* Project::findTarget(std::string_view name) {
    for (auto& t : targets_) {
        if (t->isOriginal && t->name == name) return t.get();
    }
    return nullptr;
}

}  // namespace lithium

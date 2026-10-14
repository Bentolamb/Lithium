#include "lithium/project.h"

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

void BlockContainer::setInputLiteral(BlockIndex block, std::string name, Value value) {
    Input input;
    input.kind = Input::Kind::Literal;
    input.literal = std::move(value);
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

BlockIndex BlockContainer::getNextBlock(BlockIndex block) const {
    return valid(block) ? at(block).next : kNoBlock;
}

BlockIndex BlockContainer::getBranch(BlockIndex block, int branchNum) const {
    if (!valid(block)) return kNoBlock;
    const std::string name = branchNum > 1 ? "SUBSTACK" + std::to_string(branchNum) : "SUBSTACK";
    const Input* input = at(block).findInput(name);
    if (input == nullptr || input->kind != Input::Kind::Block) return kNoBlock;
    return input->block;
}

Variable& Target::addVariable(std::string id, std::string name, Value initial) {
    variables.push_back(Variable{std::move(id), std::move(name), std::move(initial)});
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

Variable* Target::lookupVariableByName(std::string_view name) {
    for (auto& v : variables) {
        if (v.name == name) return &v;
    }
    if (!isStage && stage_ != nullptr) {
        for (auto& v : stage_->variables) {
            if (v.name == name) return &v;
        }
    }
    return nullptr;
}

Variable& Target::lookupOrCreateVariable(std::string_view id, std::string_view name) {
    if (Variable* v = lookupVariableById(id)) return *v;
    if (Variable* v = lookupVariableByName(name)) return *v;
    return addVariable(std::string(id), std::string(name), Value::number(0.0));
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

Target* Project::findTarget(std::string_view name) {
    for (auto& t : targets_) {
        if (t->name == name) return t.get();
    }
    return nullptr;
}

}  // namespace lithium

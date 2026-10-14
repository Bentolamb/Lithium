#include "lithium/sb3.h"

#include <algorithm>
#include <set>
#include <unordered_map>

#include "nlohmann/json.hpp"
#include "primitives.h"

namespace lithium {

namespace {

using Json = nlohmann::ordered_json;  // keeps block order: script order depends on it

// SB3 primitive type codes.
constexpr int kPrimVariable = 12;
constexpr int kPrimList = 13;
constexpr int kPrimBroadcast = 11;

Value valueFromJson(const Json& j) {
    if (j.is_string()) return Value::string(j.get<std::string>());
    if (j.is_boolean()) return Value::boolean(j.get<bool>());
    if (j.is_number()) return Value::number(j.get<double>());
    return Value::string("");
}

class TargetLoader {
public:
    TargetLoader(Target& target, const Json& blocks, std::vector<std::string>& warnings,
                 std::set<std::string>& unknownOpcodes)
        : target_(target), blocksJson_(blocks), warnings_(warnings), unknown_(unknownOpcodes) {}

    void load() {
        BlockContainer& bc = target_.blocks;
        // Pass 1: create blocks in file order, so script order matches scratch-vm.
        for (auto it = blocksJson_.begin(); it != blocksJson_.end(); ++it) {
            if (!it.value().is_object()) {
                // Top-level variable/list reporters are not scripts.
                warnings_.push_back(target_.name + ": loose primitive block '" + it.key() + "' ignored");
                continue;
            }
            const Json& b = it.value();
            const std::string opcode = b.value("opcode", std::string());
            if (opcode.empty()) {
                warnings_.push_back(target_.name + ": block '" + it.key() + "' has no opcode");
                continue;
            }
            const BlockIndex idx = bc.create(opcode, it.key());
            if (b.value("topLevel", false)) bc.setTopLevel(idx);
            if (lookupPrimitive(opcode) == nullptr) unknown_.insert(opcode);
        }
        // Pass 2: links, inputs, fields.
        for (auto it = blocksJson_.begin(); it != blocksJson_.end(); ++it) {
            if (!it.value().is_object()) continue;
            const BlockIndex idx = bc.find(it.key());
            if (idx == kNoBlock) continue;
            const Json& b = it.value();
            if (b.contains("next") && b["next"].is_string()) {
                const BlockIndex next = bc.find(b["next"].get<std::string>());
                if (next != kNoBlock) bc.setNext(idx, next);
            }
            if (b.contains("inputs") && b["inputs"].is_object()) {
                for (auto in = b["inputs"].begin(); in != b["inputs"].end(); ++in) {
                    loadInput(idx, in.key(), in.value());
                }
            }
            if (b.contains("mutation") && b["mutation"].is_object()) loadMutation(idx, b["mutation"]);
            if (b.contains("fields") && b["fields"].is_object()) {
                for (auto f = b["fields"].begin(); f != b["fields"].end(); ++f) {
                    loadField(idx, f.key(), f.value());
                }
            }
        }
    }

private:
    // A primitive such as [4,"10"] or [12,"name","id"]. Variables and lists
    // become real reporter blocks; everything else is a literal.
    void primitiveToInput(BlockIndex owner, const std::string& name, const Json& prim) {
        BlockContainer& bc = target_.blocks;
        if (!prim.is_array() || prim.empty() || !prim[0].is_number()) {
            warnings_.push_back(target_.name + ": unreadable input '" + name + "'");
            return;
        }
        const int type = prim[0].get<int>();
        if (type == kPrimVariable && prim.size() >= 3) {
            const BlockIndex reporter = bc.create("data_variable", "__var" + std::to_string(synthetic_++));
            bc.setField(reporter, "VARIABLE", valueFromJson(prim[1]), prim[2].is_string() ? prim[2].get<std::string>() : "");
            bc.setInputBlock(owner, name, reporter);
        } else if (type == kPrimList && prim.size() >= 3) {
            const BlockIndex reporter = bc.create("data_listcontents", "__list" + std::to_string(synthetic_++));
            bc.setField(reporter, "LIST", valueFromJson(prim[1]), prim[2].is_string() ? prim[2].get<std::string>() : "");
            bc.setInputBlock(owner, name, reporter);
        } else if (type == kPrimBroadcast && prim.size() >= 3) {
            bc.setInputLiteral(owner, name, valueFromJson(prim[1]), prim[2].is_string() ? prim[2].get<std::string>() : "");
        } else if (prim.size() >= 2) {
            bc.setInputLiteral(owner, name, valueFromJson(prim[1]));
        } else {
            bc.setInputLiteral(owner, name, Value::string(""));
        }
    }

    // Reference to a block (string id) or an inline primitive (array).
    // Returns false if it could not be resolved.
    void referenceToInput(BlockIndex owner, const std::string& name, const Json& ref, bool isBranch) {
        BlockContainer& bc = target_.blocks;
        if (ref.is_string()) {
            const BlockIndex target = bc.find(ref.get<std::string>());
            if (target == kNoBlock) {
                warnings_.push_back(target_.name + ": input '" + name + "' points at a missing block");
            } else if (isBranch) {
                bc.setBranch(owner, name, target);
            } else {
                bc.setInputBlock(owner, name, target);
            }
        } else if (ref.is_array()) {
            primitiveToInput(owner, name, ref);
        } else if (isBranch) {
            bc.setBranch(owner, name, kNoBlock);
        }
    }

    void loadInput(BlockIndex owner, const std::string& name, const Json& in) {
        if (!in.is_array() || in.size() < 2) return;
        const bool isBranch = (name == "SUBSTACK" || name == "SUBSTACK2");
        const int shadowKind = in[0].is_number() ? in[0].get<int>() : 0;
        const Json& first = in[1];
        // Kind 3 is "a block over a shadow": the block wins when present.
        // Kind 1 is shadow-only, kind 2 is block-only.
        if (shadowKind == 3 && first.is_null() && in.size() > 2) {
            referenceToInput(owner, name, in[2], false);
        } else {
            referenceToInput(owner, name, first, isBranch);
        }
    }

    // JSON array stored as a string (SB3 quirk), or as a real array.
    static Json parseJsonArray(const Json& j) {
        if (j.is_array()) return j;
        if (j.is_string()) {
            Json parsed = Json::parse(j.get<std::string>(), nullptr, false);
            if (parsed.is_array()) return parsed;
        }
        return Json::array();
    }

    void loadMutation(BlockIndex owner, const Json& m) {
        if (!m.contains("proccode") || !m["proccode"].is_string()) return;
        Mutation mut;
        mut.proccode = m["proccode"].get<std::string>();
        if (m.contains("argumentids")) {
            for (const Json& s : parseJsonArray(m["argumentids"])) mut.argumentIds.push_back(s.is_string() ? s.get<std::string>() : "");
        }
        if (m.contains("argumentnames")) {
            for (const Json& s : parseJsonArray(m["argumentnames"])) mut.argumentNames.push_back(s.is_string() ? s.get<std::string>() : "");
        }
        if (m.contains("argumentdefaults")) {
            for (const Json& s : parseJsonArray(m["argumentdefaults"])) mut.argumentDefaults.push_back(valueFromJson(s));
        }
        if (m.contains("warp")) {
            const Json& w = m["warp"];
            if (w.is_boolean()) mut.warp = w.get<bool>();
            else if (w.is_string()) mut.warp = w.get<std::string>() == "true";
        }
        target_.blocks.setMutation(owner, std::move(mut));
    }

    void loadField(BlockIndex owner, const std::string& name, const Json& f) {
        if (!f.is_array() || f.empty()) return;
        std::string id;
        if (f.size() > 1 && f[1].is_string()) id = f[1].get<std::string>();
        target_.blocks.setField(owner, name, valueFromJson(f[0]), id);
    }

    Target& target_;
    const Json& blocksJson_;
    std::vector<std::string>& warnings_;
    std::set<std::string>& unknown_;
    int synthetic_ = 0;
};

void loadVariables(Target& target, const Json& vars) {
    if (!vars.is_object()) return;
    for (auto it = vars.begin(); it != vars.end(); ++it) {
        const Json& v = it.value();
        if (!v.is_array() || v.empty()) continue;
        const std::string name = v[0].is_string() ? v[0].get<std::string>() : it.key();
        target.addVariable(it.key(), name, v.size() > 1 ? valueFromJson(v[1]) : Value::number(0));
    }
}

void loadLists(Target& target, const Json& lists) {
    if (!lists.is_object()) return;
    for (auto it = lists.begin(); it != lists.end(); ++it) {
        const Json& l = it.value();
        if (!l.is_array() || l.empty()) continue;
        std::vector<Value> items;
        if (l.size() > 1 && l[1].is_array()) {
            for (const Json& item : l[1]) items.push_back(valueFromJson(item));
        }
        target.addList(it.key(), l[0].is_string() ? l[0].get<std::string>() : it.key(), std::move(items));
    }
}

void loadBroadcasts(Target& target, const Json& broadcasts) {
    if (!broadcasts.is_object()) return;
    for (auto it = broadcasts.begin(); it != broadcasts.end(); ++it) {
        target.addBroadcast(it.key(), it.value().is_string() ? it.value().get<std::string>() : it.key());
    }
}

}  // namespace

Sb3LoadResult loadSb3Json(std::string_view text) {
    Sb3LoadResult result;
    Json root;
    try {
        root = Json::parse(text.begin(), text.end());
    } catch (const std::exception& e) {
        result.error = std::string("project.json is not valid JSON: ") + e.what();
        return result;
    }
    if (!root.is_object() || !root.contains("targets") || !root["targets"].is_array()) {
        result.error = "project.json has no targets array (is this an SB3 project?)";
        return result;
    }
    const Json& targets = root["targets"];

    // Layer order: stage first, then sprites bottom to top.
    std::vector<const Json*> sprites;
    const Json* stageJson = nullptr;
    for (const Json& t : targets) {
        if (!t.is_object()) continue;
        if (t.value("isStage", false)) {
            if (stageJson == nullptr) stageJson = &t;
        } else {
            sprites.push_back(&t);
        }
    }
    if (stageJson == nullptr) {
        result.error = "project has no stage";
        return result;
    }
    std::stable_sort(sprites.begin(), sprites.end(), [](const Json* a, const Json* b) {
        return a->value("layerOrder", 0) < b->value("layerOrder", 0);
    });

    Project project;
    std::set<std::string> unknownOpcodes;
    std::vector<std::pair<Target*, const Json*>> loaded;

    loaded.emplace_back(&project.stage(), stageJson);
    for (const Json* s : sprites) {
        loaded.emplace_back(&project.addSprite(s->value("name", std::string("Sprite"))), s);
    }
    for (auto& [target, j] : loaded) {
        if (!target->isStage) {
            target->x = j->value("x", 0.0);
            target->y = j->value("y", 0.0);
            target->direction = j->value("direction", 90.0);
            target->size = j->value("size", 100.0);
            target->visible = j->value("visible", true);
        }
        if (j->contains("variables")) loadVariables(*target, (*j)["variables"]);
        if (j->contains("lists")) loadLists(*target, (*j)["lists"]);
        if (j->contains("broadcasts")) loadBroadcasts(*target, (*j)["broadcasts"]);
        if (j->contains("blocks") && (*j)["blocks"].is_object()) {
            TargetLoader(*target, (*j)["blocks"], result.warnings, unknownOpcodes).load();
        }
    }
    for (const std::string& op : unknownOpcodes) {
        result.warnings.push_back("opcode '" + op + "' is not implemented; those blocks are skipped");
    }
    result.project = std::move(project);
    return result;
}

}  // namespace lithium

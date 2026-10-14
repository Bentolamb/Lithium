#include "lithium/builtin_projects.h"

#include "lithium/builder.h"

namespace lithium {

using namespace build;

std::optional<Project> builtinProject(std::string_view name) {
    Project p;
    Target& stage = p.stage();
    auto& b = stage.blocks;
    stage.addVariable("count", "count", Value::number(0));
    if (name == "repeat_counter") {
        // when flag clicked: set count to 0; repeat 10 { change count by 1 }
        script(b, {hatFlag(b), setVar(b, "count", Value::number(0)),
                   repeat(b, Value::number(10), changeVar(b, "count", Value::number(1)))});
        return p;
    }
    if (name == "forever_counter") {
        script(b, {hatFlag(b), setVar(b, "count", Value::number(0)),
                   forever(b, changeVar(b, "count", Value::number(1)))});
        return p;
    }
    if (name == "wait") {
        script(b, {hatFlag(b), setVar(b, "count", Value::number(0)), wait(b, Value::number(0.52)),
                   setVar(b, "count", Value::number(1))});
        return p;
    }
    if (name == "slide") {
        Target& s = p.addSprite("Sprite1");
        auto& sb = s.blocks;
        script(sb, {hatFlag(sb), gotoXY(sb, Value::number(0), Value::number(0)),
                    repeat(sb, Value::number(10), changeX(sb, Value::number(10)))});
        return p;
    }
    return std::nullopt;
}

}  // namespace lithium

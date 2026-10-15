// Lithium Lab, Packin and OutList.
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "lithium/outlist.h"
#include "lithium/packin.h"
#include "lithium/runtime.h"
#include "lithium/sb3.h"
#include "lithium/storage.h"

using namespace lithium;

namespace {

// Block JSON helpers.
std::string lit(const std::string& s) { return "[1,[10,\"" + s + "\"]]"; }
std::string rep(const std::string& id) { return "[3,\"" + id + "\",[10,\"\"]]"; }
std::string cmd(const std::string& id, const std::string& op, const std::string& inputs, const std::string& fields,
                const std::string& next, bool top = false) {
    return "\"" + id + "\":{\"opcode\":\"" + op + "\",\"next\":" + (next.empty() ? "null" : "\"" + next + "\"") +
           (top ? ",\"topLevel\":true" : "") + ",\"inputs\":{" + inputs + "},\"fields\":{" + fields + "}}";
}
std::string reporter(const std::string& id, const std::string& op, const std::string& parent, const std::string& inputs,
                     const std::string& fields = "") {
    return "\"" + id + "\":{\"opcode\":\"" + op + "\",\"next\":null,\"parent\":\"" + parent + "\",\"inputs\":{" + inputs +
           "},\"fields\":{" + fields + "}}";
}
std::string setVar(const std::string& id, const std::string& var, const std::string& valueInput, const std::string& next) {
    return cmd(id, "data_setvariableto", "\"VALUE\":" + valueInput, "\"VARIABLE\":[\"" + var + "\",\"" + var + "\"]", next);
}
const std::string kList = "\"LIST\":[\"L\",\"L\"]";

struct Sim {
    Runtime rt;
    explicit Sim(const std::string& blocks, StorageBackend* storage = nullptr, const std::string& scope = "proj") {
        const std::string json = R"({"targets":[
          {"isStage":true,"name":"Stage","variables":{"r":["r",0],"e":["e",""],"x":["x",""]},"lists":{"L":["L",["a","b","c",1,2.5]]},
           "blocks":{)" + blocks + R"(},"costumes":[{"name":"b","assetId":"x","md5ext":"x.svg","dataFormat":"svg"}]}]})";
        auto r = loadSb3Json(json);
        REQUIRE(r.project.has_value());
        double now = 0;
        rt.setClock([n = now]() mutable { return n += 17; });
        if (storage != nullptr) rt.setStorage(storage);
        rt.setStorageScope(scope);
        rt.loadProject(std::move(*r.project));
    }
    void run() {
        rt.greenFlag();
        for (int i = 0; i < 40 && (i == 0 || rt.threadCount() > 0); i++) rt.step();
        REQUIRE(rt.threadCount() == 0);
    }
    Value var(const char* n) { return *rt.variableValue("Stage", n); }
    std::vector<Value>& list() { return rt.project().stage().lookupOrCreateList("L", "L").list; }
};

std::string flag(const std::string& next) { return cmd("h", "event_whenflagclicked", "", "", next, true); }

}  // namespace

TEST_CASE("packin: round trips, modes and corruption") {
    std::mt19937 g(7);
    for (int trial = 0; trial < 300; trial++) {
        std::string d;
        const int n = static_cast<int>(g() % 500), alpha = 1 + static_cast<int>(g() % 30);
        for (int i = 0; i < n; i++) d.push_back(static_cast<char>(g() % alpha));
        const int level = 1 + static_cast<int>(g() % 9);
        const auto packed = packin::compress(d, level);
        REQUIRE(packin::decompress(packed) == d);
        const auto h = packin::readHeader(packed);
        REQUIRE(h.has_value());
        REQUIRE(h->originalLength == d.size());
    }
    std::string text;
    for (int i = 0; i < 500; i++) text += "the quick brown fox ";
    const auto packed = packin::compress(text);
    REQUIRE(packed.size() < text.size() / 20);
    std::string numbers;
    for (int i = 0; i < 2000; i++) numbers += std::to_string(1000 + i * 3) + ",";
    const auto np = packin::compress(numbers);
    REQUIRE(packin::readHeader(np)->mode >= 2);
    REQUIRE(packin::decompress(np) == numbers);
    for (std::size_t i = 0; i < packed.size(); i += 3) {
        auto bad = packed;
        bad[i] = static_cast<char>(bad[i] ^ 0x5a);
        const auto r = packin::decompress(bad);
        REQUIRE((!r || *r == text));
    }
    REQUIRE_FALSE(packin::decompress(packed.substr(0, packed.size() - 1)).has_value());
    REQUIRE_FALSE(packin::decompress("nonsense").has_value());
    REQUIRE_FALSE(packin::readHeader("PKN").has_value());
}

TEST_CASE("packin blocks") {
    std::string blocks = flag("s1") + "," +
        setVar("s1", "x", rep("dec"), "s2") + "," +
        reporter("dec", "packin_decompress", "s1", "\"DATA\":" + rep("cmp")) + "," +
        reporter("cmp", "packin_compress", "dec", "\"DATA\":" + lit("hello hello hello hello")) + "," +
        setVar("s2", "r", rep("osz"), "s3") + "," +
        reporter("osz", "packin_originalSize", "s2", "\"DATA\":" + rep("cmp2")) + "," +
        reporter("cmp2", "packin_compressAtLevel", "osz", "\"DATA\":" + lit("hello hello hello hello") + ",\"LEVEL\":" + lit("9")) + "," +
        setVar("s3", "e", rep("val"), "") + "," +
        reporter("val", "packin_isValid", "s3", "\"DATA\":" + lit("not packin"));
    Sim sim(blocks);
    sim.run();
    REQUIRE(sim.var("x").toString() == "hello hello hello hello");
    REQUIRE(sim.var("r").toNumber() == 23);
    REQUIRE_FALSE(sim.var("e").toBoolean());
}

TEST_CASE("lithium lab reports its environment") {
    std::string blocks = flag("s1") + "," +
        setVar("s1", "r", rep("a"), "s2") + "," + reporter("a", "lithiumlab_isLithium", "s1", "") + "," +
        setVar("s2", "e", rep("b"), "s3") + "," + reporter("b", "lithiumlab_isTurboWarp", "s2", "") + "," +
        setVar("s3", "x", rep("c"), "") + "," + reporter("c", "lithiumlab_isCompiled", "s3", "");
    Sim sim(blocks);
    sim.run();
    REQUIRE(sim.var("r").toBoolean());
    REQUIRE_FALSE(sim.var("e").toBoolean());
    REQUIRE_FALSE(sim.var("x").toBoolean());
    auto avail = [](const char* name) {
        std::string b = flag("s1") + "," + setVar("s1", "r", rep("a"), "") + "," +
                        reporter("a", "lithiumlab_isExtensionAvailable", "s1", std::string("\"NAME\":") + lit(name));
        Sim s(b);
        s.run();
        return s.var("r").toBoolean();
    };
    REQUIRE(avail("Packin"));
    REQUIRE(avail("Lithium Lab"));
    REQUIRE(avail("outlist"));
    REQUIRE(avail("pen"));
    REQUIRE_FALSE(avail("motion"));
    REQUIRE_FALSE(avail("nonsense"));
    REQUIRE_FALSE(avail(""));
}

TEST_CASE("lithium lab boolean parameters answer outside a procedure") {
    std::string blocks = flag("s1") + "," + setVar("s1", "r", rep("a"), "s2") + "," +
        "\"a\":{\"opcode\":\"argument_reporter_boolean\",\"next\":null,\"parent\":\"s1\",\"inputs\":{},\"fields\":{\"VALUE\":[\"is Lithium?\",null]}}," +
        setVar("s2", "e", rep("b"), "") + "," +
        "\"b\":{\"opcode\":\"argument_reporter_boolean\",\"next\":null,\"parent\":\"s2\",\"inputs\":{},\"fields\":{\"VALUE\":[\"is TurboWarp?\",null]}}";
    Sim sim(blocks);
    sim.run();
    REQUIRE(sim.var("r").toBoolean());
    REQUIRE_FALSE(sim.var("e").toBoolean());
}

TEST_CASE("outlist file format") {
    std::vector<Value> items;
    for (int i = 0; i < 10000; i++) items.push_back(Value::number(i * 0.25));
    items.push_back(Value::string("text"));
    items.push_back(Value::boolean(true));
    items.push_back(Value::number(-0.0));
    items.push_back(Value::string(""));
    items.push_back(Value::number(1e300));
    const std::string file = outlist::encode(items);
    std::string err;
    const auto back = outlist::decode(file, err);
    REQUIRE(back.has_value());
    REQUIRE(back->size() == items.size());
    for (std::size_t i = 0; i < items.size(); i++) REQUIRE((*back)[i].identical(items[i]));
    REQUIRE(outlist::readInfo(file, err)->itemCount == items.size());
    REQUIRE(outlist::fingerprint(*back) == outlist::fingerprint(items));
    // damage anywhere is detected, never accepted as different data
    std::mt19937 g(3);
    for (int i = 0; i < 400; i++) {
        auto bad = file;
        bad[g() % bad.size()] ^= static_cast<char>(1u << (g() % 8));
        const auto r = outlist::decode(bad, err);
        REQUIRE((!r || r->size() == items.size()));
    }
    REQUIRE_FALSE(outlist::decode(file.substr(0, file.size() / 2), err).has_value());
    REQUIRE_FALSE(outlist::decode("", err).has_value());
    REQUIRE(outlist::decode(outlist::encode({}), err)->empty());
}

TEST_CASE("outlist name validation") {
    for (const char* ok : {"data", "My List 2", "größe", "a.b", "x-y_z"}) REQUIRE(outlist::validateName(ok).empty());
    for (const char* bad : {"", "../x", "a/b", "a\\b", "C:x", ".hidden", "trail.", " pad", "pad ", "a%41", "a*b", "con\tt"}) {
        INFO(bad);
        REQUIRE_FALSE(outlist::validateName(bad).empty());
    }
    REQUIRE_FALSE(outlist::validateName(std::string(101, 'a')).empty());
    REQUIRE(encodeStorageName("Ab c") == "%41b%20c");
    REQUIRE(decodeStorageName(encodeStorageName("größe/Ü")) == "größe/Ü");
}

TEST_CASE("outlist blocks save, load, exist, delete") {
    MemoryStorage store;
    std::string blocks = flag("save") + "," +
        cmd("save", "outlist_saveList", "\"NAME\":" + lit("data"), kList, "ex") + "," +
        setVar("ex", "r", rep("exists"), "clear") + "," +
        reporter("exists", "outlist_exists", "ex", "\"NAME\":" + lit("data")) + "," +
        cmd("clear", "data_deletealloflist", "", kList, "load") + "," +
        cmd("load", "outlist_loadList", "\"NAME\":" + lit("data"), kList, "err") + "," +
        setVar("err", "e", rep("lasterr"), "") + "," +
        reporter("lasterr", "outlist_lastError", "err", "");
    Sim sim(blocks, &store);
    sim.run();
    REQUIRE(sim.var("r").toBoolean());
    REQUIRE(sim.var("e").toString() == "");
    REQUIRE(sim.list().size() == 5);
    REQUIRE(sim.list()[0].toString() == "a");
    REQUIRE(sim.list()[3].isNumber());
    REQUIRE(sim.list()[4].toNumber() == 2.5);

    // a new runtime with the same storage and scope sees the file, a different scope does not
    std::string check = flag("s") + "," + setVar("s", "r", rep("exists"), "") + "," +
                        reporter("exists", "outlist_exists", "s", "\"NAME\":" + lit("data"));
    Sim same(check, &store, "proj");
    same.run();
    REQUIRE(same.var("r").toBoolean());
    Sim other(check, &store, "other project");
    other.run();
    REQUIRE_FALSE(other.var("r").toBoolean());

    // delete
    std::string del = flag("d") + "," + cmd("d", "outlist_deleteList", "\"NAME\":" + lit("data"), "", "s") + "," +
                      setVar("s", "r", rep("exists"), "") + "," + reporter("exists", "outlist_exists", "s", "\"NAME\":" + lit("data"));
    Sim deleter(del, &store, "proj");
    deleter.run();
    REQUIRE_FALSE(deleter.var("r").toBoolean());
}

TEST_CASE("outlist errors leave the list alone") {
    MemoryStorage store;
    auto errorOf = [&](const std::string& op, const std::string& name) {
        std::string b = flag("c") + "," + cmd("c", op, "\"NAME\":" + lit(name), kList, "err") + "," +
                        setVar("err", "e", rep("le"), "") + "," + reporter("le", "outlist_lastError", "err", "");
        Sim s(b, &store);
        s.run();
        REQUIRE(s.list().size() == 5);
        return s.var("e").toString();
    };
    REQUIRE(errorOf("outlist_loadList", "missing").find("no saved list") != std::string::npos);
    REQUIRE(errorOf("outlist_saveList", "../escape").find("invalid name") != std::string::npos);
    REQUIRE(errorOf("outlist_loadList", "a/b").find("invalid name") != std::string::npos);
}

TEST_CASE("outlist unload only when safe") {
    MemoryStorage store;
    auto run = [&](const std::string& blocks) {
        Sim s(blocks, &store);
        s.run();
        return s.var("e").toString();
    };
    const std::string le = setVar("err", "e", rep("le"), "") + "," + reporter("le", "outlist_lastError", "err", "");
    // never saved: refused
    REQUIRE(run(flag("u") + "," + cmd("u", "outlist_unloadList", "", kList, "err") + "," + le).find("not saved") != std::string::npos);
    // saved then changed: refused
    REQUIRE(run(flag("s") + "," + cmd("s", "outlist_saveList", "\"NAME\":" + lit("d"), kList, "a") + "," +
                cmd("a", "data_addtolist", "\"ITEM\":" + lit("new"), kList, "u") + "," +
                cmd("u", "outlist_unloadList", "", kList, "err") + "," + le).find("unsaved") != std::string::npos);
    // saved: list is released, loading restores it, saving an unloaded list is refused
    {
        std::string b = flag("s") + "," + cmd("s", "outlist_saveList", "\"NAME\":" + lit("d"), kList, "u") + "," +
                        cmd("u", "outlist_unloadList", "", kList, "n") + "," +
                        setVar("n", "r", rep("len"), "sv") + "," + reporter("len", "data_lengthoflist", "n", "", kList) + "," +
                        cmd("sv", "outlist_saveList", "\"NAME\":" + lit("d"), kList, "err") + "," + le;
        Sim s(b, &store);
        s.run();
        REQUIRE(s.var("r").toNumber() == 0);
        REQUIRE(s.var("e").toString().find("unloaded") != std::string::npos);
        REQUIRE(s.list().empty());
    }
    {
        Sim s(flag("l") + "," + cmd("l", "outlist_loadList", "\"NAME\":" + lit("d"), kList, ""), &store);
        s.run();
        REQUIRE(s.list().size() == 5);  // the refused save did not overwrite the file
    }
}

TEST_CASE("outlist survives a restart with file storage") {
    const auto dir = std::filesystem::temp_directory_path() / "lithium_outlist_test";
    std::filesystem::remove_all(dir);
    {
        FileStorage fs(dir.string());
        Sim s(flag("s") + "," + cmd("s", "outlist_saveList", "\"NAME\":" + lit("Big List"), kList, ""), &fs);
        s.run();
    }
    {
        FileStorage fs(dir.string());  // a new process would build this again
        std::string b = flag("c") + "," + cmd("c", "data_deletealloflist", "", kList, "l") + "," +
                        cmd("l", "outlist_loadList", "\"NAME\":" + lit("Big List"), kList, "");
        Sim s(b, &fs);
        s.run();
        REQUIRE(s.list().size() == 5);
        REQUIRE(s.list()[2].toString() == "c");
    }
    // damage the file on disk: load reports it and the list is untouched
    for (const auto& e : std::filesystem::recursive_directory_iterator(dir)) {
        if (e.path().extension() != ".outlist") continue;
        std::fstream f(e.path(), std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(-3, std::ios::end);
        f.put('\x7f');
    }
    {
        FileStorage fs(dir.string());
        std::string b = flag("l") + "," + cmd("l", "outlist_loadList", "\"NAME\":" + lit("Big List"), kList, "err") + "," +
                        setVar("err", "e", rep("le"), "") + "," + reporter("le", "outlist_lastError", "err", "");
        Sim s(b, &fs);
        s.run();
        REQUIRE(s.var("e").toString().find("damaged") != std::string::npos);
        REQUIRE(s.list().size() == 5);
    }
    // no stray temporary files, and every file name is filesystem safe
    for (const auto& e : std::filesystem::recursive_directory_iterator(dir)) {
        const auto n = e.path().filename().string();
        REQUIRE(n.find("..") == std::string::npos);
        REQUIRE(n.find(".tmp") == std::string::npos);
    }
    std::filesystem::remove_all(dir);
}

TEST_CASE("outlist list dropdown fields carry no id") {
    MemoryStorage store;
    const std::string byName = "\"LIST\":[\"L\",null]";
    Sim s(flag("a") + "," + cmd("a", "outlist_saveList", "\"NAME\":" + lit("n"), byName, "b") + "," +
              cmd("b", "data_deletealloflist", "", kList, "c") + "," + cmd("c", "outlist_loadList", "\"NAME\":" + lit("n"), byName, ""),
          &store);
    s.run();
    REQUIRE(s.list().size() == 5);
}

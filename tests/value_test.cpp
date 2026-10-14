#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

#include "lithium/value.h"

using lithium::Value;

TEST_CASE("toNumber follows Cast.toNumber") {
    REQUIRE(Value::string("").toNumber() == 0);
    REQUIRE(Value::string("  12 ").toNumber() == 12);
    REQUIRE(Value::string("0x1F").toNumber() == 31);
    REQUIRE(Value::string("1e3").toNumber() == 1000);
    REQUIRE(Value::string("abc").toNumber() == 0);  // NaN -> 0
    REQUIRE(Value::string("Infinity").toNumber() == std::numeric_limits<double>::infinity());
    REQUIRE(Value::string("1,5").toNumber() == 0);
    REQUIRE(Value::boolean(true).toNumber() == 1);
    REQUIRE(Value::number(std::nan("")).toNumber() == 0);
}

TEST_CASE("toString follows JavaScript String(number)") {
    REQUIRE(Value::number(100).toString() == "100");
    REQUIRE(Value::number(-0.0).toString() == "0");
    REQUIRE(Value::number(0.1 + 0.2).toString() == "0.30000000000000004");
    REQUIRE(Value::number(1e21).toString() == "1e+21");
    REQUIRE(Value::number(1.5e-7).toString() == "1.5e-7");
    REQUIRE(Value::number(123456789012345680000.0).toString() == "123456789012345680000");
    REQUIRE(Value::number(std::nan("")).toString() == "NaN");
    REQUIRE(Value::number(-std::numeric_limits<double>::infinity()).toString() == "-Infinity");
    REQUIRE(Value::boolean(false).toString() == "false");
}

TEST_CASE("toBoolean follows Cast.toBoolean") {
    REQUIRE_FALSE(Value::string("").toBoolean());
    REQUIRE_FALSE(Value::string("0").toBoolean());
    REQUIRE_FALSE(Value::string("False").toBoolean());
    REQUIRE(Value::string("0.0").toBoolean());
    REQUIRE(Value::string("abc").toBoolean());
    REQUIRE_FALSE(Value::number(0).toBoolean());
    REQUIRE_FALSE(Value::number(std::nan("")).toBoolean());
    REQUIRE(Value::number(-1).toBoolean());
}

TEST_CASE("compare follows Cast.compare") {
    REQUIRE(Value::compare(Value::string("10"), Value::string("9")) > 0);   // numeric
    REQUIRE(Value::compare(Value::string("a"), Value::string("B")) < 0);    // case-insensitive
    REQUIRE(Value::compare(Value::string("ABC"), Value::string("abc")) == 0);
    REQUIRE(Value::compare(Value::string(" "), Value::number(0)) != 0);     // whitespace is not a number
    REQUIRE(Value::compare(Value::string(""), Value::number(0)) < 0);
    REQUIRE(Value::compare(Value::string("1"), Value::number(1)) == 0);
    REQUIRE(Value::compare(Value::boolean(true), Value::number(1)) == 0);
    REQUIRE(Value::compare(Value::string("Infinity"), Value::string("Infinity")) == 0);
}

TEST_CASE("jsRound ties go toward +Infinity") {
    REQUIRE(lithium::jsRound(2.5) == 3);
    REQUIRE(lithium::jsRound(-2.5) == -2);
    REQUIRE(lithium::jsRound(-0.4) == 0);
}

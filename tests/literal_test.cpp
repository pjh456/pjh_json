#include <doctest/doctest.h>
#include <ostream>
#include "pjh_json/json.hpp"
#include "pjh_json/json_constexpr.hpp"
#include "pjh_json/document.hpp"
#include "pjh_json/writer.hpp"
#include "pjh_json/detail/literal.hpp"
#include "pjh_json/detail/utils.hpp"

#include <cstring>
#include <stdexcept>
#include <string>

using namespace pjh::json;

TEST_CASE("Literal: constexpr JSON scalars") {
    constexpr Json j_null;
    static_assert(j_null.is_null());
    static_assert(!j_null.is_boolean());
    static_assert(!j_null.is_int());
    static_assert(!j_null.is_float());
    static_assert(!j_null.is_string());

    constexpr Json j_bool = true;
    static_assert(j_bool.is_boolean());

    constexpr Json j_int = int64_t(42);
    static_assert(j_int.is_int());

    constexpr Json j_double = 3.14;
    static_assert(j_double.is_float());

    constexpr Json j_str = std::string_view("hello");
    static_assert(j_str.is_string());

    REQUIRE(j_int.try_as_int().value_or(0) == 42);
    REQUIRE(j_double.try_as_float().value_or(0.0) == 3.14);
    REQUIRE(j_str.try_as_string().value_or("") == "hello");
    REQUIRE(j_bool.try_as_boolean().value_or(false));
}

TEST_CASE("Literal: constexpr integral floating") {
    constexpr Json j_int = 100;
    static_assert(j_int.is_int());
    REQUIRE(*j_int.try_as_int() == 100);

    constexpr Json j_float = 2.718f;
    static_assert(j_float.is_float());

    constexpr Json j_char = static_cast<short>(-1);
    static_assert(j_char.is_int());

    constexpr Json j_unsigned = 42u;
    static_assert(j_unsigned.is_int());
}

TEST_CASE("Literal: constexpr type checks") {
    constexpr Json j_null;
    constexpr Json j_bool = false;
    constexpr Json j_int = int64_t(0);
    constexpr Json j_float = 0.0;
    constexpr Json j_str = std::string_view("");

    static_assert(j_null.is_null());
    static_assert(j_bool.is_boolean());
    static_assert(j_int.is_number());
    static_assert(j_float.is_number());
    static_assert(!j_null.is_number());
    static_assert(j_str.is_string());
}

TEST_CASE("Literal: constexpr string") {
    constexpr String s_empty;
    static_assert(!s_empty.is_owned());
    REQUIRE(static_cast<std::string_view>(s_empty).empty());

    constexpr String s1 = "hello";
    static_assert(!s1.is_owned());
    REQUIRE(s1 == "hello");
    REQUIRE(s1 == std::string_view("hello"));

    constexpr String s2 = std::string_view("world");
    REQUIRE(s1 != s2);

    // Moved-from is a compile-time null view (not just a runtime contract).
    static_assert([] {
        String a = "hi";
        String b(std::move(a));
        return static_cast<std::string_view>(a).empty() &&
               static_cast<std::string_view>(a).data() == nullptr &&
               static_cast<std::string_view>(b) == std::string_view("hi");
    }());
}

TEST_CASE("Literal: magic constants") {
    char buf[8]{};
    std::memcpy(buf, &kTrueMagic, 4);
    REQUIRE(std::string_view(buf, 4) == "true");

    std::memcpy(buf, &kNullMagic, 4);
    REQUIRE(std::string_view(buf, 4) == "null");

    static_assert((kFalseMagic & kFalseMask) == kFalseMagic);
}

TEST_CASE("Literal: throw_parse_error") {
    CHECK_THROWS_AS(throw_parse_error("test error", "xxx", "xxx"), ParseError);

    // Task 13: one definition for all build modes — "<msg> at offset N"
    // (release no longer flattens to the literal "parse error").
    char data[] = "hello world";
    try {
        throw_parse_error("test error", data + 4, data);
        REQUIRE(false);
    } catch (const ParseError &e) {
        REQUIRE(std::string(e.what()).find("test error at offset 4") != std::string::npos);
        REQUIRE(e.offset() == 4);
    }

    const char b[] = "xxx";
    try {
        throw_parse_error("test error", b, b);
        REQUIRE(false);
    } catch (const ParseError &e) {
        REQUIRE(std::string(e.what()).find("test error at offset 0") != std::string::npos);
        REQUIRE(e.offset() == 0);
    }
}

TEST_CASE("Literal: array literal roundtrip") {
    auto arr = ConstJson::of(1, 2, 3, std::string_view("hello"), true, nullptr);
    REQUIRE(arr.size() == 6);

    Json runtime_arr = arr.to_runtime();
    REQUIRE(runtime_arr.size() == 6);
    REQUIRE(runtime_arr[0] == int64_t(1));
    REQUIRE(runtime_arr[1] == int64_t(2));
    REQUIRE(runtime_arr[2] == int64_t(3));
    REQUIRE(runtime_arr[3] == std::string_view("hello"));
    REQUIRE(runtime_arr[4] == true);
    REQUIRE(runtime_arr[5] == nullptr);
}

TEST_CASE("Literal: object literal roundtrip") {
    auto obj = ConstJson::of(
        kv("name", std::string_view("alice")),
        kv("age", int64_t(30)),
        kv("score", 99.5),
        kv("active", true)
    );

    REQUIRE(obj.size() == 4);

    Json runtime_obj = obj.to_runtime();
    REQUIRE(runtime_obj.size() == 4);
    REQUIRE(runtime_obj["name"] == std::string_view("alice"));
    REQUIRE(runtime_obj["age"] == int64_t(30));
    REQUIRE(runtime_obj["score"] == 99.5);
    REQUIRE(runtime_obj["active"] == true);
}

TEST_CASE("Literal: trailing garbage rejection") {
    CHECK_THROWS_AS(parse_copy("truee"), ParseError);
    CHECK_THROWS_AS(parse_copy("falsex"), ParseError);
    CHECK_THROWS_AS(parse_copy("tru"), ParseError);
}

TEST_CASE("Literal: dump") {
    Json json_root = ConstJson::of(1, -2, 3).to_runtime();

    std::pmr::string out = dump(json_root, DumpOptions{});
    REQUIRE(out == "[1,-2,3]");

    auto pretty = dump(json_root, DumpOptions{.pretty = true});
    REQUIRE(pretty.find("[") != std::pmr::string::npos);
    REQUIRE(pretty.find("1") != std::pmr::string::npos);
    REQUIRE(pretty.find("-2") != std::pmr::string::npos);
    REQUIRE(pretty.find("3") != std::pmr::string::npos);
}

TEST_CASE("Literal: empty object") {
    auto empty_arr = ConstJson::of();
    REQUIRE(empty_arr.size() == 0);

    Json j = empty_arr.to_runtime();
    std::pmr::string out = dump(j);
    REQUIRE(out == "[]");
}

TEST_CASE("ConstJson: parse strictness") {
    // Leading zeros (runtime reference: src/parser/number.cpp, pinned in
    // "Parser: error handling")
    static_assert(!ConstJson::parse("01").valid);
    static_assert(!ConstJson::parse("-01").valid);
    static_assert(!ConstJson::parse("[01]").valid);
    static_assert(!ConstJson::parse("{\"a\":01}").valid);
    static_assert(!ConstJson::parse("012.5").valid);
    static_assert(!ConstJson::parse("01e2").valid);
    static_assert(ConstJson::parse("0").valid);
    static_assert(ConstJson::parse("-0").valid);
    static_assert(ConstJson::parse("0.5").valid);
    static_assert(ConstJson::parse("0e1").valid);
    // Escapes (runtime reference: src/parser/utils.cpp)
    static_assert(!ConstJson::parse(std::string_view("\"\\q\"", 4)).valid);  // illegal single-char escape
    static_assert(!ConstJson::parse(std::string_view("\"\\", 2)).valid);    // dangling backslash
    static_assert(!ConstJson::parse(R"("\u12")").valid);                    // truncated \uXXXX
    static_assert(!ConstJson::parse(R"("\uZZZZ")").valid);                  // non-hex digits
    static_assert(!ConstJson::parse(R"("\uD800")").valid);                  // lone high surrogate
    static_assert(!ConstJson::parse(R"("\uDC00")").valid);                  // lone low surrogate
    static_assert(!ConstJson::parse(R"("\uD800A")").valid);                 // high not followed by \u
    static_assert(!ConstJson::parse(R"("\uD800\u0041")").valid);            // second half not a low surrogate
    static_assert(!ConstJson::parse(R"("\uD800\uD800")").valid);            // second half is a high surrogate
    static_assert(ConstJson::parse(R"("\uD83D\uDE00")").valid);             // legal surrogate pair
    static_assert(ConstJson::parse(R"("\u0000")").valid);
    static_assert(ConstJson::parse("\"\\n\\t\\r\\b\\f\\\"\\\\\\/\"").valid); // all short escapes
    // Raw C0 control characters in strings (runtime reference: src/parser/string.cpp)
    static_assert(!ConstJson::parse(std::string_view("\"a\nb\"", 5)).valid);     // raw LF
    static_assert(!ConstJson::parse(std::string_view("\"a\x01" "b\"", 5)).valid);  // raw 0x01
    static_assert(!ConstJson::parse(std::string_view("\"a\x00" "b\"", 5)).valid);  // raw NUL
    static_assert(ConstJson::parse(std::string_view("\"a\x7f" "b\"", 5)).valid); // DEL (0x7F) is legal in strings
    // Whitespace set regression pins (already aligned, must not regress)
    static_assert(ConstJson::parse(" \t\r\n 42 \t").valid);
    static_assert(!ConstJson::parse(std::string_view("\x0b" "1", 2)).valid);  // VT is not whitespace
    static_assert(!ConstJson::parse(std::string_view("1\x0b", 2)).valid);

    // BOM (task 23): the constexpr path is grammar-strict — 0xEF is not
    // whitespace (validate.hpp:26-30) and no consteval knob can strip it
    // (std::atomic is not constexpr). Runtime strip_bom does NOT apply
    // here: the divergence is by design (§1.4).
    static_assert(!ConstJson::parse(std::string_view("\xEF\xBB\xBF" "1", 4)).valid);
    static_assert(!ConstJson::parse(std::string_view("\xEF\xBB\xBF", 3)).valid);
    static_assert(ConstJson::parse("1").valid); // control: BOM-free is fine

    // Raw-byte leniency (task 24 documented divergence): the constexpr
    // validator stays raw-byte-lenient — the runtime strict_utf8 knob
    // cannot reach consteval (std::atomic is not constexpr).
    static_assert(ConstJson::parse(std::string_view("\"a\xFF" "b\"", 5)).valid);  // 0xFF passes (>= 0x20)
    static_assert(ConstJson::parse(std::string_view("\"a\xC0" "b\"", 5)).valid);  // overlong lead passes raw

    // Range divergence (task 61): `valid` is a GRAMMAR verdict only. The
    // constexpr validator has no double-magnitude concept, so out-of-range
    // magnitudes pass it, while to_document() applies the runtime range gate
    // and throws a positioned ParseError (same documented-divergence family
    // as the strip_bom / strict_utf8 knobs above).
    static_assert(ConstJson::parse("1e400").valid);
    static_assert(ConstJson::parse("1e-400").valid);
    static_assert(ConstJson::parse("1e309").valid);
    static_assert(ConstJson::parse("0e400").valid);
    CHECK_THROWS_AS(ConstJson::parse("1e400").to_document(), ParseError);
    CHECK_THROWS_AS(ConstJson::parse("1e-400").to_document(), ParseError);
    // Control: a finite in-range magnitude materializes without throwing.
    REQUIRE(ConstJson::parse("1e308").to_document().root().is_float());
}

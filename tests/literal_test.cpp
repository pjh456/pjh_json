#include <doctest/doctest.h>
#include "pjh_json/json.hpp"
#include "pjh_json/json_constexpr.hpp"
#include "pjh_json/document.hpp"
#include "pjh_json/writer.hpp"
#include "pjh_json/utils.hpp"

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

#ifdef NDEBUG
    try {
        const char b[] = "xxx";
        throw_parse_error("test error", b, b);
        REQUIRE(false);
    } catch (const ParseError &e) {
        REQUIRE(std::string(e.what()) == "parse error");
    }
#else
    try {
        char data[] = "hello world";
        throw_parse_error("test error", data + 4, data);
        REQUIRE(false);
    } catch (const ParseError &e) {
        REQUIRE(std::string(e.what()).find("test error at offset 4") != std::string::npos);
    }
#endif
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

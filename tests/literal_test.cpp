#include "pjh_json/json.hpp"
#include "pjh_json/document.hpp"
#include "pjh_json/writer.hpp"
#include "pjh_json/utils.hpp"

#include <cassert>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace pjh::json;

// --- constexpr Json construction ---

void test_constexpr_json_scalars()
{
    std::cout << "test_constexpr_json_scalars..." << std::endl;

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

    // runtime checks
    assert(j_int.try_as_int().value_or(0) == 42);
    assert(j_double.try_as_float().value_or(0.0) == 3.14);
    assert(j_str.try_as_string().value_or("") == "hello");
    assert(j_bool.try_as_boolean().value_or(false));

    std::cout << "  passed." << std::endl;
}

void test_constexpr_json_integral_floating()
{
    std::cout << "test_constexpr_json_integral_floating..." << std::endl;

    constexpr Json j_int = 100;
    static_assert(j_int.is_int());
    assert(*j_int.try_as_int() == 100);

    constexpr Json j_float = 2.718f;
    static_assert(j_float.is_float());

    constexpr Json j_char = static_cast<short>(-1);
    static_assert(j_char.is_int());

    constexpr Json j_unsigned = 42u;
    static_assert(j_unsigned.is_int());

    std::cout << "  passed." << std::endl;
}

void test_constexpr_json_type_checks()
{
    std::cout << "test_constexpr_json_type_checks..." << std::endl;

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

    std::cout << "  passed." << std::endl;
}

// --- constexpr String ---

void test_constexpr_string()
{
    std::cout << "test_constexpr_string..." << std::endl;

    constexpr String s_empty;
    static_assert(!s_empty.is_owned());
    assert(static_cast<std::string_view>(s_empty).empty());

    constexpr String s1 = "hello";
    static_assert(!s1.is_owned());
    assert(s1 == "hello");
    assert(s1 == std::string_view("hello"));

    constexpr String s2 = std::string_view("world");
    assert(s1 != s2);

    std::cout << "  passed." << std::endl;
}

// --- magic constants ---

void test_literal_magic_constants()
{
    std::cout << "test_literal_magic_constants..." << std::endl;

    char buf[8]{};
    std::memcpy(buf, &kTrueMagic, 4);
    assert(std::string_view(buf, 4) == "true");

    std::memcpy(buf, &kNullMagic, 4);
    assert(std::string_view(buf, 4) == "null");

    static_assert((kFalseMagic & kFalseMask) == kFalseMagic);

    std::cout << "  passed." << std::endl;
}

// --- throw_parse_error ---

void test_throw_parse_error()
{
    std::cout << "test_throw_parse_error..." << std::endl;

    try {
        const char b[] = "xxx";
        throw_parse_error("test error", b, b);
        assert(false);
    } catch (const ParseError &) {
        // expected
    }

#ifdef NDEBUG
    try {
        const char b[] = "xxx";
        throw_parse_error("test error", b, b);
        assert(false);
    } catch (const ParseError &e) {
        assert(std::string(e.what()) == "parse error");
    }
#else
    try {
        char data[] = "hello world";
        throw_parse_error("test error", data + 4, data);
        assert(false);
    } catch (const ParseError &e) {
        assert(std::string(e.what()).find("test error at offset 4") != std::string::npos);
    }
#endif

    std::cout << "  passed." << std::endl;
}

// --- consteval array literal + runtime bridge ---

void test_array_literal_roundtrip()
{
    std::cout << "test_array_literal_roundtrip..." << std::endl;

    auto arr = json_array_literal(1, 2, 3, std::string_view("hello"), true, nullptr);

    assert(arr.size() == 6);
    assert(arr[0].is_int());
    assert(arr[3].is_string());
    assert(arr[4].is_boolean());
    assert(arr[5].is_null());
    assert(arr[3] == std::string_view("hello"));

    auto runtime_arr = array_from_literal(std::move(arr));
    assert(runtime_arr.size() == 6);
    assert(runtime_arr[0] == int64_t(1));
    assert(runtime_arr[1] == int64_t(2));
    assert(runtime_arr[2] == int64_t(3));
    assert(runtime_arr[3] == std::string_view("hello"));
    assert(runtime_arr[4] == true);
    assert(runtime_arr[5] == nullptr);

    std::cout << "  passed." << std::endl;
}

// --- consteval kv + object literal + runtime bridge ---

void test_object_literal_roundtrip()
{
    std::cout << "test_object_literal_roundtrip..." << std::endl;

    auto entries = std::array{
        kv("name", std::string_view("alice")),
        kv("age", int64_t(30)),
        kv("score", 99.5),
        kv("active", true),
    };

    assert(entries.size() == 4);
    assert(entries[0].first == "name");
    assert(entries[1].second.is_int());

    auto runtime_obj = object_from_literal(std::move(entries));
    assert(runtime_obj.size() == 4);
    assert(runtime_obj["name"] == std::string_view("alice"));
    assert(runtime_obj["age"] == int64_t(30));
    assert(runtime_obj["score"] == 99.5);
    assert(runtime_obj["active"] == true);

    std::cout << "  passed." << std::endl;
}

// --- literal trailing garbage rejection ---

void test_literal_trailing_garbage()
{
    std::cout << "test_literal_trailing_garbage..." << std::endl;

    bool threw = false;
    try {
        (void)parse_copy("truee");
    } catch (const ParseError &) {
        threw = true;
    }
    assert(threw);

    threw = false;
    try {
        (void)parse_copy("falsex");
    } catch (const ParseError &) {
        threw = true;
    }
    assert(threw);

    threw = false;
    try {
        (void)parse_copy("tru");
    } catch (const ParseError &) {
        threw = true;
    }
    assert(threw);

    std::cout << "  passed." << std::endl;
}

// --- dump array from literal ---

void test_literal_dump()
{
    std::cout << "test_literal_dump..." << std::endl;

    auto arr = json_array_literal(1, -2, 3);
    auto doc = array_from_literal(std::move(arr));
    auto json_root = Json(std::move(doc));

    std::pmr::string out = dump(json_root, DumpOptions{});
    assert(out == "[1,-2,3]");

    auto pretty = dump(json_root, DumpOptions{.pretty = true});
    assert(pretty.find("[") != std::pmr::string::npos);
    assert(pretty.find("1") != std::pmr::string::npos);
    assert(pretty.find("-2") != std::pmr::string::npos);
    assert(pretty.find("3") != std::pmr::string::npos);

    std::cout << "  passed." << std::endl;
}

// --- empty object literal ---

void test_literal_empty_object()
{
    std::cout << "test_literal_empty_object..." << std::endl;

    auto entries = std::array<Object::Entry, 0>{};
    auto obj = object_from_literal(std::move(entries));
    assert(obj.size() == 0);
    assert(obj.empty());

    std::pmr::string out = dump(Json(std::move(obj)));
    assert(out == "{}");

    std::cout << "  passed." << std::endl;
}

int main()
{
    std::cout << "--- Starting Literal Tests ---" << std::endl;

    test_constexpr_json_scalars();
    test_constexpr_json_integral_floating();
    test_constexpr_string();
    test_constexpr_json_type_checks();
    test_literal_magic_constants();
    test_throw_parse_error();
    test_array_literal_roundtrip();
    test_object_literal_roundtrip();
    test_literal_dump();
    test_literal_empty_object();
    test_literal_trailing_garbage();

    std::cout << "--- All Literal Tests Passed Successfully! ---" << std::endl;
    return 0;
}

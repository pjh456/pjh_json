#include <doctest/doctest.h>
#include <string_view>
#include <cstring>
#include <stdexcept>
#include <cmath>

#include <pjh_json/document.hpp>
#include <pjh_json/parser.hpp>

using namespace pjh::json;

TEST_CASE("Parser: literal") {
    auto doc1 = parse_copy("null");
    REQUIRE(doc1.root().is_null());

    auto doc2 = parse_copy("true");
    REQUIRE(doc2.root().is_boolean());
    REQUIRE(doc2.root().as_boolean() == true);

    auto doc3 = parse_copy("false");
    REQUIRE(doc3.root().is_boolean());
    REQUIRE(doc3.root().as_boolean() == false);

    auto doc4 = parse_copy(" \t\r\n   true \n\t ");
    REQUIRE(doc4.root().is_boolean());
    REQUIRE(doc4.root().as_boolean() == true);
}

TEST_CASE("Parser: number") {
    auto doc1 = parse_copy("42");
    REQUIRE(doc1.root().as_int() == (int64_t)42);

    auto doc2 = parse_copy("-12345");
    REQUIRE(doc2.root().as_int() == (int64_t)-12345);

    auto doc3 = parse_copy("3.14159");
    REQUIRE(doc3.root().is_float());
    REQUIRE(std::abs(doc3.root().as_float() - 3.14159) < 1e-6);

    auto doc4 = parse_copy("-0.05e2");
    REQUIRE(doc4.root().is_float());
    REQUIRE(std::abs(doc4.root().as_float() - (-5.0)) < 1e-6);
}

TEST_CASE("Parser: string") {
    auto doc1 = parse_copy("\"hello world\"");
    REQUIRE(doc1.root().as_string() == "hello world");

    auto doc2 = parse_copy("\"line1\\nline2\\t\\\"quote\\\"\"");
    REQUIRE(doc2.root().as_string() == "line1\nline2\t\"quote\"");

    auto doc3 = parse_copy("\"\\uD83D\\uDE00\"");
    REQUIRE(doc3.root().as_string() == "\xF0\x9F\x98\x80");

    auto doc4 = parse_copy("\"this is a very long string without any escape characters to test simd acceleration speed\"");
    REQUIRE(doc4.root().as_string() == "this is a very long string without any escape characters to test simd acceleration speed");
}

TEST_CASE("Parser: array") {
    auto doc1 = parse_copy("[]");
    REQUIRE(doc1.root().is_array());
    REQUIRE(doc1.root().size() == 0);

    auto doc2 = parse_copy("[1, 2, 3]");
    REQUIRE(doc2.root().is_array());
    REQUIRE(doc2.root().size() == 3);
    REQUIRE(doc2.root()[0] == (int64_t)1);
    REQUIRE(doc2.root()[1] == (int64_t)2);
    REQUIRE(doc2.root()[2] == (int64_t)3);

    auto doc3 = parse_copy("[[1, 2], [3, 4]]");
    REQUIRE(doc3.root().is_array());
    REQUIRE(doc3.root().size() == 2);
    REQUIRE(doc3.root()[0][1] == (int64_t)2);
    REQUIRE(doc3.root()[1][0] == (int64_t)3);
}

TEST_CASE("Parser: object") {
    auto doc1 = parse_copy("{}");
    REQUIRE(doc1.root().is_object());
    REQUIRE(doc1.root().size() == 0);

    auto doc2 = parse_copy("{\"name\": \"pjh\", \"version\": 1}");
    REQUIRE(doc2.root().is_object());
    REQUIRE(doc2.root().size() == 2);
    REQUIRE(doc2.root()["name"] == "pjh");
    REQUIRE(doc2.root()["version"] == (int64_t)1);

    auto doc3 = parse_copy("{\"user\": {\"id\": 100, \"active\": true}}");
    REQUIRE(doc3.root()["user"].is_object());
    REQUIRE(doc3.root()["user"]["id"] == (int64_t)100);
    REQUIRE(doc3.root()["user"]["active"] == true);
}

TEST_CASE("Parser: complex") {
    std::string_view complex_json = R"({
        "project": "pjh_json",
        "description": "SIMD accelerated JSON parser",
        "is_fast": true,
        "features": ["simd", "header-only", "c++20"],
        "metadata": {
            "version": 1.0,
            "author": null
        }
    })";

    auto doc = parse_copy(complex_json);

    REQUIRE(doc.root().is_object());
    REQUIRE(doc.root()["project"] == "pjh_json");
    REQUIRE(doc.root()["is_fast"] == true);

    REQUIRE(doc.root()["features"].is_array());
    REQUIRE(doc.root()["features"].size() == 3);
    REQUIRE(doc.root()["features"][0] == "simd");

    REQUIRE(doc.root()["metadata"].is_object());
    REQUIRE(doc.root()["metadata"]["version"].is_float());
    REQUIRE(doc.root()["metadata"]["author"].is_null());
}

TEST_CASE("Parser: error handling") {
    Config::instance().set_strict_duplicate_keys(true);

    CHECK_THROWS_AS((void)parse_copy(""), std::runtime_error);
    CHECK_THROWS_AS((void)parse_copy("{"), std::runtime_error);
    CHECK_THROWS_AS((void)parse_copy("["), std::runtime_error);
    CHECK_THROWS_AS((void)parse_copy("\"unterminated"), std::runtime_error);
    CHECK_THROWS_AS((void)parse_copy("{\"key\": }"), std::runtime_error);
    CHECK_THROWS_AS((void)parse_copy("[1, 2,]"), std::runtime_error);
    CHECK_THROWS_AS((void)parse_copy("true false"), std::runtime_error);
    CHECK_THROWS_AS((void)parse_copy("01"), std::runtime_error);
    CHECK_THROWS_AS((void)parse_copy("1."), std::runtime_error);
    CHECK_THROWS_AS((void)parse_copy("1e"), std::runtime_error);
    CHECK_THROWS_AS((void)parse_copy("1e+"), std::runtime_error);
    CHECK_THROWS_AS((void)parse_copy("-"), std::runtime_error);
    CHECK_THROWS_AS((void)parse_copy("-.1"), std::runtime_error);
    CHECK_THROWS_AS((void)parse_copy("{\"a\":1,\"a\":2}"), std::runtime_error);
}

TEST_CASE("Parser: view") {
    std::string content = R"({"name": "pjh", "items": [1, 2, 3], "active": true})";
    std::string buf(content.size() + 64, '\0');
    memcpy(buf.data(), content.data(), content.size());

    auto doc = parse_view(buf.data(), content.size());
    REQUIRE(doc.is_view());
    REQUIRE(doc.root().is_object());
    REQUIRE(doc.root()["name"] == "pjh");
    REQUIRE(doc.root()["active"] == true);
    REQUIRE(doc.root()["items"].is_array());
    REQUIRE(doc.root()["items"].size() == 3);

    std::string esc_content = R"({"msg": "hello\nworld"})";
    std::string esc_buf(esc_content.size() + 64, '\0');
    memcpy(esc_buf.data(), esc_content.data(), esc_content.size());

    auto doc2 = parse_view(esc_buf.data(), esc_content.size());
    REQUIRE(doc2.is_view());
    REQUIRE(doc2.root().is_object());
    REQUIRE(doc2.root()["msg"] == "hello\nworld");
}

TEST_CASE("Parser: whitespace rejects C0 control bytes") {
    // RFC 8259 §2: whitespace is exactly space (0x20), tab (0x09),
    // LF (0x0A), CR (0x0D). Every other C0 control byte is illegal in
    // both value-before and value-after positions.
    for (int c = 1; c < 0x20; ++c)
    {
        if (c == 0x09 || c == 0x0A || c == 0x0D)
            continue;
        std::string lead = std::string(1, static_cast<char>(c)) + "1";
        CHECK_THROWS_AS((void)parse_copy(lead), std::runtime_error);
        std::string trail = "1";
        trail += static_cast<char>(c);
        CHECK_THROWS_AS((void)parse_copy(trail), std::runtime_error);
    }
    // container-internal positions (object key->value gap, array element gap)
    std::string obj = std::string("{\"k\" ") + static_cast<char>(0x0B) + " : 1}";
    CHECK_THROWS_AS((void)parse_copy(obj), std::runtime_error);
    std::string arr = std::string("[1 ") + static_cast<char>(0x1B) + " , 2]";
    CHECK_THROWS_AS((void)parse_copy(arr), std::runtime_error);
}

TEST_CASE("Parser: NUL and DEL are not whitespace") {
    CHECK_THROWS_AS((void)parse_copy(std::string(1, '\0') + "1"), std::runtime_error);
    CHECK_THROWS_AS((void)parse_copy(std::string("1") + std::string(1, '\0')), std::runtime_error);
    CHECK_THROWS_AS((void)parse_copy(std::string(1, char(0x7F))), std::runtime_error);
}

TEST_CASE("Parser: jsonl control byte line rejected") {
    // A line made of an illegal control byte must error, not silently hop
    // into the next line and duplicate its value.
    CHECK_THROWS_AS(
        (void)parse_jsonl(std::string(1, char(0x0B)) + "\n[1]\n"),
        std::runtime_error);
    CHECK_THROWS_AS(
        (void)parse_jsonl(std::string(1, '\0') + "\n[1]\n"),
        std::runtime_error);
    // A whitespace-only line (CR/space) is blank: skipped, not duplicated.
    auto doc_ws = parse_jsonl(std::string("\r \r\n[1]\n"));
    REQUIRE(doc_ws.root().is_array());
    REQUIRE(doc_ws.root().size() == 1);
    REQUIRE(doc_ws.root()[0][0] == (int64_t)1);
    // Legal jsonl still parses.
    auto doc = parse_jsonl("1\n2\n");
    REQUIRE(doc.root().is_array());
    REQUIRE(doc.root().size() == 2);
    REQUIRE(doc.root()[0] == (int64_t)1);
    REQUIRE(doc.root()[1] == (int64_t)2);
}

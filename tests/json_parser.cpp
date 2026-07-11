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
    REQUIRE(doc2.root()["msg"] == "hello\nworld");
}

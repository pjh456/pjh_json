#include <doctest/doctest.h>
#include "pjh_json/json.hpp"
#include "pjh_json/json_constexpr.hpp"
#include "pjh_json/document.hpp"

using namespace pjh::json;

TEST_CASE("ConstJson: of_array") {
    auto arr = ConstJson::of(1, 2.5, std::string_view("hello"), true, nullptr);
    REQUIRE(arr.size() == 5);
    Json j = arr.to_runtime();
    REQUIRE(j[0].as_int() == 1);
    REQUIRE(j[3].as_boolean() == true);
}

TEST_CASE("ConstJson: of_object") {
    auto obj = ConstJson::of(
        kv("int", 42), kv("double", 3.14), kv("str", std::string_view("hi")),
        kv("bool", true), kv("null", nullptr));
    REQUIRE(obj.size() == 5);
    Json j = obj.to_runtime();
    REQUIRE(j["int"] == int64_t(42));
}

TEST_CASE("ConstJson: of_nested") {
    auto root = ConstJson::of(
        kv("name", "Alice"),
        kv("items", ConstJson::of(1, 2, 3))
    );
    Json j = root.to_runtime();
    REQUIRE(j["name"].as_string() == "Alice");
    REQUIRE(j["items"].size() == 3);
    REQUIRE(j["items"][0].as_int() == 1);
    REQUIRE(j["items"][2].as_int() == 3);
}

TEST_CASE("ConstJson: of_deep_nest") {
    auto root = ConstJson::of(
        kv("outer", ConstJson::of(
            kv("inner", ConstJson::of(1, 2, 3))
        ))
    );
    Json j = root.to_runtime();
    REQUIRE(j["outer"]["inner"][2].as_int() == 3);
    REQUIRE(j["outer"]["inner"][0].as_int() == 1);
}

TEST_CASE("ConstJson: parse") {
    auto pr = ConstJson::parse(R"({"a":1,"b":"hello","c":[1,2,3]})");
    REQUIRE(pr.valid);
    auto doc = pr.to_document();
    REQUIRE(doc.root()["a"].as_int() == 1);
    REQUIRE(doc.root()["b"].as_string() == "hello");
    REQUIRE(doc.root()["c"].size() == 3);
}

TEST_CASE("ConstJson: scalar") {
    constexpr auto cj = to_const_json(42);
    REQUIRE(cj.v == 42);
    Json j = to_runtime(cj);
    REQUIRE(j.as_int() == 42);
}

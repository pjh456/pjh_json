#include <doctest/doctest.h>
#include <ostream>
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

TEST_CASE("ConstJson: compile-time access") {
    // ---- kind trait / kind_v ----
    static_assert(ConstJsonNull::kind_v == ConstJsonKind::Null);
    static_assert(ConstJsonBool::kind_v == ConstJsonKind::Bool);
    static_assert(ConstJsonInt::kind_v == ConstJsonKind::Int);
    static_assert(ConstJsonDouble::kind_v == ConstJsonKind::Double);
    static_assert(ConstJsonStr::kind_v == ConstJsonKind::String);
    static_assert(ConstJsonArray<>::kind_v == ConstJsonKind::Array);
    static_assert(ConstJsonObject<>::kind_v == ConstJsonKind::Object);
    static_assert(const_json_kind_v<ConstJsonInt> == ConstJsonKind::Int);
    static_assert(const_json_kind_v<const ConstJsonStr &> == ConstJsonKind::String);

    // ---- is_* predicates on scalars ----
    static_assert(is_null(ConstJsonNull{}));
    static_assert(!is_bool(ConstJsonNull{}));
    static_assert(is_bool(ConstJsonBool{true}));
    static_assert(!is_int(ConstJsonBool{true}));
    static_assert(is_int(ConstJsonInt{42}));
    static_assert(!is_double(ConstJsonInt{42}));
    static_assert(is_double(ConstJsonDouble{2.5}));
    static_assert(!is_string(ConstJsonDouble{2.5}));
    static_assert(is_string(ConstJsonStr{"hi"}));
    static_assert(!is_null(ConstJsonStr{"hi"}));
    static_assert(!is_array(ConstJsonInt{1}));
    static_assert(!is_object(ConstJsonInt{1}));

    // ---- as_* accessors ----
    static_assert(as_bool(ConstJsonBool{true}));
    static_assert(!as_bool(ConstJsonBool{false}));
    static_assert(as_int(ConstJsonInt{-7}) == -7);
    static_assert(as_int(ConstJsonInt{0}) == 0);
    static_assert(as_double(ConstJsonDouble{2.5}) == 2.5);
    static_assert(as_string(ConstJsonStr{"hi"}) == "hi");
    static_assert(as_string(ConstJsonStr{""}).empty());

    // ---- array: get<I>, front, size ----
    constexpr auto arr = ConstJson::of(1, 2.5, std::string_view("hi"), true, nullptr);
    static_assert(is_array(arr));
    static_assert(!is_object(arr));
    static_assert(arr.size() == 5);
    static_assert(is_int(get<0>(arr)) && as_int(get<0>(arr)) == 1);
    static_assert(is_double(get<1>(arr)) && as_double(get<1>(arr)) == 2.5);
    static_assert(is_string(get<2>(arr)) && as_string(get<2>(arr)) == "hi");
    static_assert(is_bool(get<3>(arr)) && as_bool(get<3>(arr)));
    static_assert(is_null(get<4>(arr)));
    static_assert(is_int(front(arr)) && as_int(front(arr)) == 1);
    static_assert(is_int(front(ConstJson::of(9))));

    // ---- empty array ----
    constexpr auto empty_arr = ConstJson::of();
    static_assert(is_array(empty_arr));
    static_assert(empty_arr.size() == 0);

    // ---- object: get<I>, has_key ----
    constexpr auto obj = ConstJson::of(kv("a", 1), kv("b", std::string_view("x")),
                                       kv("c", true));
    static_assert(is_object(obj));
    static_assert(!is_array(obj));
    static_assert(obj.size() == 3);
    static_assert(get<0>(obj).key == "a");
    static_assert(is_int(get<0>(obj).value) && as_int(get<0>(obj).value) == 1);
    static_assert(is_string(get<1>(obj).value));
    static_assert(as_string(get<1>(obj).value) == "x");
    static_assert(has_key(obj, "a"));
    static_assert(has_key(obj, "b"));
    static_assert(has_key(obj, "c"));
    static_assert(!has_key(obj, "zz"));

    // ---- empty object ----
    constexpr ConstJsonObject<> empty_obj{};
    static_assert(is_object(empty_obj) && empty_obj.size() == 0);
    static_assert(!has_key(empty_obj, "a"));
    static_assert(find<ConstJsonInt>(empty_obj, "a") == nullptr);

    // ---- find<T>: first structural match, exact wrapped type ----
    static_assert(find<ConstJsonInt>(obj, "a") != nullptr);
    static_assert(as_int(*find<ConstJsonInt>(obj, "a")) == 1);
    static_assert(find<ConstJsonStr>(obj, "b") != nullptr);
    static_assert(as_string(*find<ConstJsonStr>(obj, "b")) == "x");
    static_assert(find<ConstJsonBool>(obj, "c") != nullptr);
    static_assert(as_bool(*find<ConstJsonBool>(obj, "c")));
    static_assert(find<ConstJsonInt>(obj, "b") == nullptr);    // wrong type
    static_assert(find<ConstJsonDouble>(obj, "a") == nullptr); // wrong type
    static_assert(find<ConstJsonInt>(obj, "zz") == nullptr);   // missing key

    // ---- nested containers: object carrying object + array values ----
    constexpr auto root = ConstJson::of(
        kv("user", ConstJson::of(kv("id", 42), kv("name", std::string_view("Alice")))),
        kv("tags", ConstJson::of(1, 2, 3)));
    static_assert(is_object(root) && root.size() == 2);
    static_assert(has_key(root, "user"));
    static_assert(has_key(root, "tags"));
    static_assert(!has_key(root, "id"));
    static_assert(is_object(get<0>(root).value));
    static_assert(is_array(get<1>(root).value));
    static_assert(as_int(*find<ConstJsonInt>(get<0>(root).value, "id")) == 42);
    static_assert(find<ConstJsonInt>(get<0>(root).value, "name") == nullptr);
    static_assert(as_string(*find<ConstJsonStr>(get<0>(root).value, "name")) == "Alice");
    static_assert(get<1>(root).value.size() == 3);
    static_assert(as_int(get<0>(get<1>(root).value)) == 1);
    static_assert(as_int(get<2>(get<1>(root).value)) == 3);
    static_assert(as_int(front(get<1>(root).value)) == 1);

    // ---- direct nested-array elements: regression for decay of the value type ----
    // A container argument reaches to_const_json's identity overload as an rvalue
    // reference; the array factory must decay it or the declared element type
    // (ConstJsonArray<...>&&) disagrees with the tuple std::make_tuple stores.
    constexpr auto jagg = ConstJson::of(ConstJson::of(1, 2), ConstJson::of(3));
    static_assert(is_array(jagg) && jagg.size() == 2);
    static_assert(is_array(get<0>(jagg)) && get<0>(jagg).size() == 2);
    static_assert(as_int(get<0>(get<0>(jagg))) == 1);
    static_assert(as_int(get<1>(get<0>(jagg))) == 2);
    static_assert(as_int(front(get<1>(jagg))) == 3);
}

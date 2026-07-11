#include "pjh_json/json.hpp"
#include "pjh_json/json_constexpr.hpp"
#include "pjh_json/document.hpp"

#include <cassert>
#include <iostream>

using namespace pjh::json;

void test_of_array()
{
    std::cout << "test_of_array..." << std::endl;
    auto arr = ConstJson::of(1, 2.5, std::string_view("hello"), true, nullptr);
    assert(arr.size() == 5);
    Json j = arr.to_runtime();
    assert(j[0].as_int() == 1);
    assert(j[3].as_boolean() == true);
    std::cout << "  passed." << std::endl;
}

void test_of_object()
{
    std::cout << "test_of_object..." << std::endl;
    auto obj = ConstJson::of(
        kv("int", 42), kv("double", 3.14), kv("str", std::string_view("hi")),
        kv("bool", true), kv("null", nullptr));
    assert(obj.size() == 5);
    Json j = obj.to_runtime();
    assert(j["int"] == int64_t(42));
    std::cout << "  passed." << std::endl;
}

void test_of_nested()
{
    std::cout << "test_of_nested..." << std::endl;
    auto root = ConstJson::of(
        kv("name", "Alice"),
        kv("items", ConstJson::of(1, 2, 3))
    );
    Json j = root.to_runtime();
    assert(j["name"].as_string() == "Alice");
    assert(j["items"].size() == 3);
    assert(j["items"][0].as_int() == 1);
    assert(j["items"][2].as_int() == 3);
    std::cout << "  passed." << std::endl;
}

void test_of_deep_nest()
{
    std::cout << "test_of_deep_nest..." << std::endl;
    auto root = ConstJson::of(
        kv("outer", ConstJson::of(
            kv("inner", ConstJson::of(1, 2, 3))
        ))
    );
    Json j = root.to_runtime();
    assert(j["outer"]["inner"][2].as_int() == 3);
    assert(j["outer"]["inner"][0].as_int() == 1);
    std::cout << "  passed." << std::endl;
}

void test_parse()
{
    std::cout << "test_parse..." << std::endl;
    auto pr = ConstJson::parse(R"({"a":1,"b":"hello","c":[1,2,3]})");
    assert(pr.valid);
    auto doc = pr.to_document();
    assert(doc.root()["a"].as_int() == 1);
    assert(doc.root()["b"].as_string() == "hello");
    assert(doc.root()["c"].size() == 3);
    std::cout << "  passed." << std::endl;
}

void test_scalar()
{
    std::cout << "test_scalar..." << std::endl;
    constexpr auto cj = to_const_json(42);
    assert(cj.v == 42);
    Json j = to_runtime(cj);
    assert(j.as_int() == 42);
    std::cout << "  passed." << std::endl;
}

int main()
{
    test_of_array();
    test_of_object();
    test_of_nested();
    test_of_deep_nest();
    test_parse();
    test_scalar();
    std::cout << "All passed." << std::endl;
}

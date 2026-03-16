#include <iostream>
#include <cassert>
#include <string_view>
#include <stdexcept>
#include <cmath>

#include <pjh_json/json.hpp>

using namespace pjh::json;

void test_parse_literal()
{
    std::cout << "Parser Literal test started." << std::endl;

    Parser p1("null");
    Json j1 = p1.parse();
    assert(j1.is_null());

    Parser p2("true");
    Json j2 = p2.parse();
    assert(j2.is_boolean());
    assert(j2.as_boolean() == true);

    Parser p3("false");
    Json j3 = p3.parse();
    assert(j3.is_boolean());
    assert(j3.as_boolean() == false);

    // 测试 SIMD 对大段空白字符的跳过能力
    Parser p4(" \t\r\n   true \n\t ");
    Json j4 = p4.parse();
    assert(j4.is_boolean() && j4.as_boolean() == true);

    std::cout << "Parser Literal test passed." << std::endl;
}

void test_parse_number()
{
    std::cout << "Parser Number test started." << std::endl;

    Parser p1("42");
    assert(p1.parse().as_int() == (int64_t)42);

    Parser p2("-12345");
    assert(p2.parse().as_int() == (int64_t)-12345);

    Parser p3("3.14159");
    Json j3 = p3.parse();
    assert(j3.is_float());
    assert(std::abs(j3.as_float() - 3.14159) < 1e-6);

    Parser p4("-0.05e2");
    Json j4 = p4.parse();
    assert(j4.is_float());
    assert(std::abs(j4.as_float() - (-5.0)) < 1e-6);

    std::cout << "Parser Number test passed." << std::endl;
}

void test_parse_string()
{
    std::cout << "Parser String test started." << std::endl;

    Parser p1("\"hello world\"");
    assert(p1.parse().as_string() == "hello world");

    // 测试转义符 \n, \t, \" 等
    Parser p2("\"line1\\nline2\\t\\\"quote\\\"\"");
    assert(p2.parse().as_string() == "line1\nline2\t\"quote\"");

    // 测试 Unicode 代理对解析 (emoji: 😀 -> U+1F600)
    Parser p3("\"\\uD83D\\uDE00\"");
    assert(p3.parse().as_string() == "\xF0\x9F\x98\x80");

    // 测试包含大量普通字符的 SIMD 快速跳过
    Parser p4("\"this is a very long string without any escape characters to test simd acceleration speed\"");
    assert(p4.parse().as_string() == "this is a very long string without any escape characters to test simd acceleration speed");

    std::cout << "Parser String test passed." << std::endl;
}

void test_parse_array()
{
    std::cout << "Parser Array test started." << std::endl;

    Parser p1("[]");
    Json j1 = p1.parse();
    assert(j1.is_array());
    assert(j1.size() == 0);

    Parser p2("[1, 2, 3]");
    Json j2 = p2.parse();
    assert(j2.is_array());
    assert(j2.size() == 3);
    assert(j2[0] == (int64_t)1);
    assert(j2[1] == (int64_t)2);
    assert(j2[2] == (int64_t)3);

    // 测试嵌套数组
    Parser p3("[[1, 2], [3, 4]]");
    Json j3 = p3.parse();
    assert(j3.is_array());
    assert(j3.size() == 2);
    assert(j3[0][1] == (int64_t)2);
    assert(j3[1][0] == (int64_t)3);

    std::cout << "Parser Array test passed." << std::endl;
}

void test_parse_object()
{
    std::cout << "Parser Object test started." << std::endl;

    Parser p1("{}");
    Json j1 = p1.parse();
    assert(j1.is_object());
    assert(j1.size() == 0);

    Parser p2("{\"name\": \"pjh\", \"version\": 1}");
    Json j2 = p2.parse();
    assert(j2.is_object());
    assert(j2.size() == 2);
    assert(j2["name"] == "pjh");
    assert(j2["version"] == (int64_t)1);

    // 测试嵌套对象
    Parser p3("{\"user\": {\"id\": 100, \"active\": true}}");
    Json j3 = p3.parse();
    assert(j3["user"].is_object());
    assert(j3["user"]["id"] == (int64_t)100);
    assert(j3["user"]["active"] == true);

    std::cout << "Parser Object test passed." << std::endl;
}

void test_parse_complex()
{
    std::cout << "Parser Complex test started." << std::endl;

    // 综合测试
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

    Parser p(complex_json);
    Json j = p.parse();

    assert(j.is_object());
    assert(j["project"] == "pjh_json");
    assert(j["is_fast"] == true);

    assert(j["features"].is_array());
    assert(j["features"].size() == 3);
    assert(j["features"][0] == "simd");

    assert(j["metadata"].is_object());
    assert(j["metadata"]["version"].is_float());
    assert(j["metadata"]["author"].is_null());

    std::cout << "Parser Complex test passed." << std::endl;
}

void test_parse_errors()
{
    std::cout << "Parser Error Handling test started." << std::endl;

    // 辅助 lambda 检查是否抛出异常
    auto expect_throw = [](std::string_view invalid_json)
    {
        try
        {
            Parser p(invalid_json);
            p.parse();
            assert(false && "Should have thrown an exception");
        }
        catch (const std::runtime_error &)
        {
            // Expected behavior
        }
    };

    expect_throw("");               // 空输入
    expect_throw("{");              // 未闭合的对象
    expect_throw("[");              // 未闭合的数组
    expect_throw("\"unterminated"); // 未闭合的字符串
    expect_throw("{\"key\": }");    // 缺少 value
    expect_throw("[1, 2,]");        // 数组末尾多余的逗号
    expect_throw("true false");     // 多余的符号

    std::cout << "Parser Error Handling test passed." << std::endl;
}

int main()
{
    std::cout << "--- Starting JSON Parser Tests ---" << std::endl;

    test_parse_literal();
    test_parse_number();
    test_parse_string();
    test_parse_array();
    test_parse_object();
    test_parse_complex();
    test_parse_errors();

    std::cout << "--- All JSON Parser Tests Passed Successfully! ---" << std::endl;
    return 0;
}
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

    auto doc1 = parse_copy("null");
    assert(doc1.is_null());

    auto doc2 = parse_copy("true");
    assert(doc2.is_boolean());
    assert(doc2.as_boolean() == true);

    auto doc3 = parse_copy("false");
    assert(doc3.is_boolean());
    assert(doc3.as_boolean() == false);

    // 测试 SIMD 对大段空白字符的跳过能力
    auto doc4 = parse_copy(" \t\r\n   true \n\t ");
    assert(doc4.is_boolean() && doc4.as_boolean() == true);

    std::cout << "Parser Literal test passed." << std::endl;
}

void test_parse_number()
{
    std::cout << "Parser Number test started." << std::endl;

    auto doc1 = parse_copy("42");
    assert(doc1.as_int() == (int64_t)42);

    auto doc2 = parse_copy("-12345");
    assert(doc2.as_int() == (int64_t)-12345);

    auto doc3 = parse_copy("3.14159");
    assert(doc3.is_float());
    assert(std::abs(doc3.as_float() - 3.14159) < 1e-6);

    auto doc4 = parse_copy("-0.05e2");
    assert(doc4.is_float());
    assert(std::abs(doc4.as_float() - (-5.0)) < 1e-6);

    std::cout << "Parser Number test passed." << std::endl;
}

void test_parse_string()
{
    std::cout << "Parser String test started." << std::endl;

    auto doc1 = parse_copy("\"hello world\"");
    assert(doc1.as_string() == "hello world");

    // 测试转义符 \n, \t, \" 等
    auto doc2 = parse_copy("\"line1\\nline2\\t\\\"quote\\\"\"");
    assert(doc2.as_string() == "line1\nline2\t\"quote\"");

    // 测试 Unicode 代理对解析 (emoji: 😀 -> U+1F600)
    auto doc3 = parse_copy("\"\\uD83D\\uDE00\"");
    assert(doc3.as_string() == "\xF0\x9F\x98\x80");

    // 测试包含大量普通字符的 SIMD 快速跳过
    auto doc4 = parse_copy("\"this is a very long string without any escape characters to test simd acceleration speed\"");
    assert(doc4.as_string() == "this is a very long string without any escape characters to test simd acceleration speed");

    std::cout << "Parser String test passed." << std::endl;
}

void test_parse_array()
{
    std::cout << "Parser Array test started." << std::endl;

    auto doc1 = parse_copy("[]");
    assert(doc1.is_array());
    assert(doc1.size() == 0);

    auto doc2 = parse_copy("[1, 2, 3]");
    assert(doc2.is_array());
    assert(doc2.size() == 3);
    assert(doc2[0] == (int64_t)1);
    assert(doc2[1] == (int64_t)2);
    assert(doc2[2] == (int64_t)3);

    // 测试嵌套数组
    auto doc3 = parse_copy("[[1, 2], [3, 4]]");
    assert(doc3.is_array());
    assert(doc3.size() == 2);
    assert(doc3[0][1] == (int64_t)2);
    assert(doc3[1][0] == (int64_t)3);

    std::cout << "Parser Array test passed." << std::endl;
}

void test_parse_object()
{
    std::cout << "Parser Object test started." << std::endl;

    auto doc1 = parse_copy("{}");
    assert(doc1.is_object());
    assert(doc1.size() == 0);

    auto doc2 = parse_copy("{\"name\": \"pjh\", \"version\": 1}");
    assert(doc2.is_object());
    assert(doc2.size() == 2);
    assert(doc2["name"] == "pjh");
    assert(doc2["version"] == (int64_t)1);

    // 测试嵌套对象
    auto doc3 = parse_copy("{\"user\": {\"id\": 100, \"active\": true}}");
    assert(doc3["user"].is_object());
    assert(doc3["user"]["id"] == (int64_t)100);
    assert(doc3["user"]["active"] == true);

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

    auto doc = parse_copy(complex_json);

    assert(doc.is_object());
    assert(doc["project"] == "pjh_json");
    assert(doc["is_fast"] == true);

    assert(doc["features"].is_array());
    assert(doc["features"].size() == 3);
    assert(doc["features"][0] == "simd");

    assert(doc["metadata"].is_object());
    assert(doc["metadata"]["version"].is_float());
    assert(doc["metadata"]["author"].is_null());

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
            parse_copy(invalid_json);
            assert(false && "Should have thrown an exception");
        }
        catch (const std::runtime_error &)
        {
            // Expected behavior
        }
    };

    // parse_in_situ 要求可写缓冲且末尾有 64 字节 padding
    try
    {
        std::string buf = "null";
        parse_in_situ(std::pmr::string(buf.begin(), buf.end()));
        assert(false && "parse_in_situ should require 64-byte padding");
    }
    catch (const std::runtime_error &)
    {
        // Expected behavior
    }

    // Parser 直接使用未 padded 输入应当报错
    try
    {
        Parser p("null");
        p.parse();
        assert(false && "Parser should require padded input");
    }
    catch (const std::runtime_error &)
    {
        // Expected behavior
    }

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

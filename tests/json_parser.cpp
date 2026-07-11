#include <iostream>
#include <cassert>
#include <string_view>
#include <cstring>
#include <stdexcept>
#include <cmath>

#include <pjh_json/document.hpp>
#include <pjh_json/parser.hpp>

using namespace pjh::json;

void test_parse_literal()
{
    std::cout << "Parser Literal test started." << std::endl;

    auto doc1 = parse_copy("null");
    assert(doc1.root().is_null());

    auto doc2 = parse_copy("true");
    assert(doc2.root().is_boolean());
    assert(doc2.root().as_boolean() == true);

    auto doc3 = parse_copy("false");
    assert(doc3.root().is_boolean());
    assert(doc3.root().as_boolean() == false);

    // 测试 SIMD 对大段空白字符的跳过能力
    auto doc4 = parse_copy(" \t\r\n   true \n\t ");
    assert(doc4.root().is_boolean() && doc4.root().as_boolean() == true);

    std::cout << "Parser Literal test passed." << std::endl;
}

void test_parse_number()
{
    std::cout << "Parser Number test started." << std::endl;

    auto doc1 = parse_copy("42");
    assert(doc1.root().as_int() == (int64_t)42);

    auto doc2 = parse_copy("-12345");
    assert(doc2.root().as_int() == (int64_t)-12345);

    auto doc3 = parse_copy("3.14159");
    assert(doc3.root().is_float());
    assert(std::abs(doc3.root().as_float() - 3.14159) < 1e-6);

    auto doc4 = parse_copy("-0.05e2");
    assert(doc4.root().is_float());
    assert(std::abs(doc4.root().as_float() - (-5.0)) < 1e-6);

    std::cout << "Parser Number test passed." << std::endl;
}

void test_parse_string()
{
    std::cout << "Parser String test started." << std::endl;

    auto doc1 = parse_copy("\"hello world\"");
    assert(doc1.root().as_string() == "hello world");

    // 测试转义符 \n, \t, \" 等
    auto doc2 = parse_copy("\"line1\\nline2\\t\\\"quote\\\"\"");
    assert(doc2.root().as_string() == "line1\nline2\t\"quote\"");

    // 测试 Unicode 代理对解析 (emoji: 😀 -> U+1F600)
    auto doc3 = parse_copy("\"\\uD83D\\uDE00\"");
    assert(doc3.root().as_string() == "\xF0\x9F\x98\x80");

    // 测试包含大量普通字符的 SIMD 快速跳过
    auto doc4 = parse_copy("\"this is a very long string without any escape characters to test simd acceleration speed\"");
    assert(doc4.root().as_string() == "this is a very long string without any escape characters to test simd acceleration speed");

    std::cout << "Parser String test passed." << std::endl;
}

void test_parse_array()
{
    std::cout << "Parser Array test started." << std::endl;

    auto doc1 = parse_copy("[]");
    assert(doc1.root().is_array());
    assert(doc1.root().size() == 0);

    auto doc2 = parse_copy("[1, 2, 3]");
    assert(doc2.root().is_array());
    assert(doc2.root().size() == 3);
    assert(doc2.root()[0] == (int64_t)1);
    assert(doc2.root()[1] == (int64_t)2);
    assert(doc2.root()[2] == (int64_t)3);

    // 测试嵌套数组
    auto doc3 = parse_copy("[[1, 2], [3, 4]]");
    assert(doc3.root().is_array());
    assert(doc3.root().size() == 2);
    assert(doc3.root()[0][1] == (int64_t)2);
    assert(doc3.root()[1][0] == (int64_t)3);

    std::cout << "Parser Array test passed." << std::endl;
}

void test_parse_object()
{
    std::cout << "Parser Object test started." << std::endl;

    auto doc1 = parse_copy("{}");
    assert(doc1.root().is_object());
    assert(doc1.root().size() == 0);

    auto doc2 = parse_copy("{\"name\": \"pjh\", \"version\": 1}");
    assert(doc2.root().is_object());
    assert(doc2.root().size() == 2);
    assert(doc2.root()["name"] == "pjh");
    assert(doc2.root()["version"] == (int64_t)1);

    // 测试嵌套对象
    auto doc3 = parse_copy("{\"user\": {\"id\": 100, \"active\": true}}");
    assert(doc3.root()["user"].is_object());
    assert(doc3.root()["user"]["id"] == (int64_t)100);
    assert(doc3.root()["user"]["active"] == true);

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

    assert(doc.root().is_object());
    assert(doc.root()["project"] == "pjh_json");
    assert(doc.root()["is_fast"] == true);

    assert(doc.root()["features"].is_array());
    assert(doc.root()["features"].size() == 3);
    assert(doc.root()["features"][0] == "simd");

    assert(doc.root()["metadata"].is_object());
    assert(doc.root()["metadata"]["version"].is_float());
    assert(doc.root()["metadata"]["author"].is_null());

    std::cout << "Parser Complex test passed." << std::endl;
}

void test_parse_errors()
{
    std::cout << "Parser Error Handling test started." << std::endl;

    Config::instance().set_strict_duplicate_keys(true);

    // 辅助 lambda 检查是否抛出异常
    auto expect_throw = [](std::string_view invalid_json)
    {
        try
        {
            (void)parse_copy(invalid_json);
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
        (void)parse_in_situ(std::pmr::string(buf.begin(), buf.end()));
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
        (void)p.parse();
        assert(false && "Parser should require padded input");
    }
    catch (const std::runtime_error &)
    {
        // Expected behavior
    }

    expect_throw("");                  // 空输入
    expect_throw("{");                 // 未闭合的对象
    expect_throw("[");                 // 未闭合的数组
    expect_throw("\"unterminated");    // 未闭合的字符串
    expect_throw("{\"key\": }");       // 缺少 value
    expect_throw("[1, 2,]");           // 数组末尾多余的逗号
    expect_throw("true false");        // 多余的符号
    expect_throw("01");                // 前导 0
    expect_throw("1.");                // 缺少小数位
    expect_throw("1e");                // 缺少指数
    expect_throw("1e+");               // 缺少指数值
    expect_throw("-");                 // 只有负号
    expect_throw("-.1");               // 缺少整数部分
    expect_throw("{\"a\":1,\"a\":2}"); // 重复 key

    std::cout << "Parser Error Handling test passed." << std::endl;
}

void test_parse_view()
{
    std::cout << "Parser View test started." << std::endl;

    // Create a buffer with 64 bytes of null padding after content
    std::string content = R"({"name": "pjh", "items": [1, 2, 3], "active": true})";
    std::string buf(content.size() + 64, '\0');
    memcpy(buf.data(), content.data(), content.size());

    auto doc = parse_view(buf.data(), content.size());
    assert(doc.is_view());
    assert(doc.root().is_object());
    assert(doc.root()["name"] == "pjh");
    assert(doc.root()["active"] == true);
    assert(doc.root()["items"].is_array());
    assert(doc.root()["items"].size() == 3);

    // parse_view with string containing escape sequences (copies, not in-place)
    std::string esc_content = R"({"msg": "hello\nworld"})";
    std::string esc_buf(esc_content.size() + 64, '\0');
    memcpy(esc_buf.data(), esc_content.data(), esc_content.size());

    auto doc2 = parse_view(esc_buf.data(), esc_content.size());
    assert(doc2.is_view());
    assert(doc2.root()["msg"] == "hello\nworld");

    std::cout << "Parser View test passed." << std::endl;
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
    test_parse_view();

    std::cout << "--- All JSON Parser Tests Passed Successfully! ---" << std::endl;
    return 0;
}

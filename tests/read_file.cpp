#include <iostream>
#include <fstream>
#include <cassert>
#include <cstdio> // for std::remove
#include <stdexcept>

#include <pjh_json/json.hpp>

using namespace pjh::json;

void test_file_parsing_success()
{
    std::cout << "File Parsing Success test started." << std::endl;

    std::string temp_filename = "pjh_temp_test_config.json";

    // 1. 动态创建一个测试用的 JSON 文件
    {
        std::ofstream out(temp_filename);
        out << R"({
            "engine": "xsimd",
            "version": 1.5,
            "supported_types": ["object", "array", "string", "number", "boolean", "null"],
            "is_header_only": true
        })";
    }

    // 2. 调用 parse_file 接口
    Document doc = parse_file(temp_filename);

    // 3. 验证解析结果
    assert(doc.is_object());
    assert(doc["engine"] == "xsimd");
    assert(doc["version"].is_float());
    assert(doc["supported_types"].is_array());
    assert(doc["supported_types"].size() == 6);
    assert(doc["supported_types"][1] == "array");
    assert(doc["is_header_only"] == true);

    // 4. 清理临时文件
    std::remove(temp_filename.c_str());

    std::cout << "File Parsing Success test passed." << std::endl;
}

void test_file_parsing_failure()
{
    std::cout << "File Parsing Failure test started." << std::endl;

    // 尝试读取一个不存在的文件
    bool exception_thrown = false;
    try
    {
        Json j = parse_file("this_file_absolutely_does_not_exist_999.json");
    }
    catch (const std::runtime_error &e)
    {
        exception_thrown = true;
    }

    assert(exception_thrown && "Should have thrown an exception for non-existent file.");

    std::cout << "File Parsing Failure test passed." << std::endl;
}

int main()
{
    std::cout << "--- Starting JSON File Interface Tests ---" << std::endl;

    test_file_parsing_success();
    test_file_parsing_failure();

    std::cout << "--- All File Interface Tests Passed Successfully! ---" << std::endl;
    return 0;
}

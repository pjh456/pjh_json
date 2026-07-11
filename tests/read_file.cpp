#include <doctest/doctest.h>
#include <fstream>
#include <cstdio>
#include <stdexcept>

#include <pjh_json/document.hpp>

using namespace pjh::json;

TEST_CASE("File: parsing success") {
    std::string temp_filename = "pjh_temp_test_config.json";

    {
        std::ofstream out(temp_filename);
        out << R"({
            "engine": "xsimd",
            "version": 1.5,
            "supported_types": ["object", "array", "string", "number", "boolean", "null"],
            "is_header_only": true
        })";
    }

    Document doc = parse_file(temp_filename);

    REQUIRE(doc.root().is_object());
    REQUIRE(doc.root()["engine"] == "xsimd");
    REQUIRE(doc.root()["version"].is_float());
    REQUIRE(doc.root()["supported_types"].is_array());
    REQUIRE(doc.root()["supported_types"].size() == 6);
    REQUIRE(doc.root()["supported_types"][1] == "array");
    REQUIRE(doc.root()["is_header_only"] == true);

    std::remove(temp_filename.c_str());
}

TEST_CASE("File: parsing failure") {
    REQUIRE_THROWS_AS(parse_file("this_file_absolutely_does_not_exist_999.json"), std::runtime_error);
}

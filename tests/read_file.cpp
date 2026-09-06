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
    try {
        parse_file("this_file_absolutely_does_not_exist_999.json");
        REQUIRE(false);
    } catch (const ParseError &e) {
        REQUIRE(e.offset() == 0); // context-free: no position
    }
}

TEST_CASE("File: truncated mid-object") {
    const std::string f = "pjh_trunc_obj.json";
    {
        std::ofstream out(f, std::ios::binary);
        out << R"({"a": 1)"; // 7 bytes: valid prefix, cut inside the object
    }
    try {
        (void)parse_file(f);
        REQUIRE(false);
    } catch (const ParseError &e) {
        REQUIRE(e.offset() == 7); // "Unexpected end of object" @ content end
    }
    std::remove(f.c_str());
}

TEST_CASE("File: truncated string") {
    const std::string f = "pjh_trunc_str.json";
    {
        std::ofstream out(f, std::ios::binary);
        out << R"("abc)"; // 4 bytes, no closing quote
    }
    try {
        (void)parse_file(f);
        REQUIRE(false);
    } catch (const ParseError &e) {
        REQUIRE(e.offset() == 4); // "Unterminated string" @ content end
    }
    std::remove(f.c_str());
}

TEST_CASE("File: empty file") {
    const std::string f = "pjh_empty.json";
    {
        std::ofstream out(f, std::ios::binary);
    }
    try {
        (void)parse_file(f);
        REQUIRE(false);
    } catch (const ParseError &e) {
        REQUIRE(e.offset() == 0); // "Unexpected end of input" @ content start
    }
    std::remove(f.c_str());
}

TEST_CASE("File: result entry") {
    // Missing file: context-free ParseError in the error channel (offset 0)
    auto r = parse_file_result("this_file_absolutely_does_not_exist_999.json");
    REQUIRE(r.is_err());
    ParseError e = r.unwrap_err();
    REQUIRE(e.offset() == 0);
    REQUIRE(e.category() == Category::Parse);
    REQUIRE(dynamic_cast<const ParseError *>(&e) != nullptr);

    // Success path (mirrors "File: parsing success")
    const std::string f = "pjh_result_file.json";
    {
        std::ofstream out(f, std::ios::binary);
        out << R"({"ok":1})";
    }
    auto ok = parse_file_result(f);
    REQUIRE(ok.is_ok());
    Document d = std::move(ok).unwrap();
    REQUIRE(d.root()["ok"] == (int64_t)1);
    std::remove(f.c_str());
}

TEST_CASE("File: BOM") {
    // read_file.cpp has no shared ConfigGuard TU: local RAII guard, same
    // obligation as json_parser.cpp's (doctest has no teardown)
    struct StripBomGuard
    {
        bool m_strip;

        StripBomGuard()
            : m_strip(Config::instance().strip_bom())
        {
        }

        ~StripBomGuard()
        {
            Config::instance().set_strip_bom(m_strip);
        }
    } guard;

    const std::string f = "pjh_bom_file.json";
    {
        std::ofstream out(f, std::ios::binary);
        out.write("\xEF\xBB\xBF", 3);
        out << R"({"a":1})";
    }

    // Default: the file content delegates to parse_in_situ — the leading
    // BOM is rejected at offset 0
    try {
        (void)parse_file(f);
        REQUIRE(false);
    } catch (const ParseError &e) {
        REQUIRE(e.offset() == 0);
    }

    // strip ON: the file entry and the result shell both parse
    Config::instance().set_strip_bom(true);
    auto doc = parse_file(f);
    REQUIRE(doc.root()["a"] == (int64_t)1);

    auto r = parse_file_result(f);
    REQUIRE(r.is_ok());

    std::remove(f.c_str());
}

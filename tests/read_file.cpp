#include <doctest/doctest.h>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <stdexcept>

#include <pjh_json/document.hpp>
#include <pjh_json/writer.hpp>

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

TEST_CASE("File: parse from stream round trip") {
    const std::string payload =
        R"({"engine":"xsimd","n":42,"a":[true,null,"x"],"ok":false})";

    std::istringstream ss(payload);
    Document d1 = parse_from_istream(ss);
    Document d2 = parse_copy(payload);

    // Serialization equality is the machine channel for whole-tree identity
    REQUIRE(dump(d1.root()) == dump(d2.root()));
    REQUIRE(d1.root().is_object());
    REQUIRE(d1.root()["engine"] == "xsimd");
    REQUIRE(d1.root()["n"] == (int64_t)42);
    REQUIRE(d1.root()["a"].is_array());
    REQUIRE(d1.root()["a"].size() == 3);
    REQUIRE(d1.root()["a"][2] == "x");
    REQUIRE(d1.root()["ok"] == false);

    // Buffer identity: both = payload + kPaddingWidth NUL bytes (no hidden
    // re-encoding, same pad width, complete stream content captured)
    REQUIRE(d1.buffer() == d2.buffer());
}

TEST_CASE("File: parse from stream truncated") {
    // Short stream: offset = bytes read = stream-start relative
    {
        std::istringstream t(std::string(R"({"a": 1)", 7));
        try {
            (void)parse_from_istream(t);
            REQUIRE(false);
        } catch (const ParseError &e) {
            REQUIRE(e.offset() == 7); // mirrors "File: truncated mid-object"
        }
    }

    // Deep truncation: a >2-chunk body cut mid-token must still report the
    // offset relative to the stream start, not to a chunk/buffer boundary.
    std::string cut = "[\"" + std::string(20000, 'x') + "\"]";
    cut.resize(cut.size() - 2); // lose the closing quote and bracket
    std::istringstream big(cut);
    try {
        (void)parse_from_istream(big);
        REQUIRE(false);
    } catch (const ParseError &e) {
        REQUIRE(e.offset() == cut.size());
    }
}

TEST_CASE("File: parse from stream empty or failed") {
    // Empty stream: inherits the empty-input contract (offset 0), mirroring
    // "File: empty file"
    {
        std::istringstream empty;
        try {
            (void)parse_from_istream(empty);
            REQUIRE(false);
        } catch (const ParseError &e) {
            REQUIRE(e.offset() == 0);
        }
    }

    // Entry badbit: the stream is already dead -> context-free read failure
    {
        std::istringstream bad;
        bad.setstate(std::ios_base::badbit);
        REQUIRE_THROWS_AS((void)parse_from_istream(bad), ParseError);
        try {
            (void)parse_from_istream(bad);
            REQUIRE(false);
        } catch (const ParseError &e) {
            REQUIRE(e.offset() == 0);
        }
    }
}

TEST_CASE("File: parse from stream result entry") {
    // Error channel: empty stream -> ParseError stored by value, no slicing
    std::istringstream err_in;
    auto r = parse_from_istream_result(err_in);
    REQUIRE(r.is_err());
    ParseError e = r.unwrap_err();
    REQUIRE(e.offset() == 0);
    REQUIRE(e.category() == Category::Parse);
    REQUIRE(dynamic_cast<const ParseError *>(&e) != nullptr);

    // Success channel (consuming unwrap, rvalue form)
    std::istringstream ok_in(R"({"ok":1})");
    auto ok = parse_from_istream_result(ok_in);
    REQUIRE(ok.is_ok());
    Document d = std::move(ok).unwrap();
    REQUIRE(d.root()["ok"] == (int64_t)1);
}

TEST_CASE("File: parse from stream large") {
    // 100,000 compact values, spanning ~72 kStreamChunk (8192) reads
    std::string payload = "[";
    for (int i = 0; i < 100000; ++i) {
        if (i)
            payload += ",";
        payload += std::to_string(i);
    }
    payload += "]";

    std::istringstream ss(payload);
    Document d1 = parse_from_istream(ss);
    Document d2 = parse_copy(payload);
    REQUIRE(dump(d1.root()) == dump(d2.root()));
    REQUIRE(d1.root().is_array());
    REQUIRE(d1.root().size() == 100000);
    REQUIRE(d1.root()[0] == (int64_t)0);
    REQUIRE(d1.root()[99999] == (int64_t)99999);

    // Chunk-boundary EOF exemption: pad the leading whitespace so the total
    // length is an exact multiple of 8192 (kStreamChunk). The last read is
    // then a 0-byte extraction, which implementations may mark badbit; the
    // loop must exempt it before the badbit test or this common EOF shape
    // falsely reports "Failed to read stream".
    const size_t chunk = 8192; // mirrors kStreamChunk in parse.cpp
    std::string padded((chunk - payload.size() % chunk) % chunk, ' ');
    padded += payload;
    std::istringstream ss2(padded);
    Document d3 = parse_from_istream(ss2);
    REQUIRE(dump(d3.root()) == dump(d1.root()));
}

TEST_CASE("File: parse from stream config knobs") {
    // Local RAII guard: doctest has no teardown; restore all three knobs
    struct StreamConfigGuard
    {
        size_t m_max_depth;
        bool m_strip_bom;
        bool m_strict_utf8;

        StreamConfigGuard()
            : m_max_depth(Config::instance().max_depth()),
              m_strip_bom(Config::instance().strip_bom()),
              m_strict_utf8(Config::instance().strict_utf8())
        {
        }

        ~StreamConfigGuard()
        {
            Config::instance().set_max_depth(m_max_depth);
            Config::instance().set_strip_bom(m_strip_bom);
            Config::instance().set_strict_utf8(m_strict_utf8);
        }
    } guard;

    // max_depth: the stream entry inherits the shared Parser capture
    Config::instance().set_max_depth(2);
    {
        std::istringstream deep("[[[1]]]");
        REQUIRE_THROWS_AS((void)parse_from_istream(deep), ParseError);
    }
    REQUIRE_THROWS_AS((void)parse_copy("[[[1]]]"), ParseError);
    {
        std::istringstream ok("[[1]]");
        Document d = parse_from_istream(ok);
        REQUIRE(d.root().is_array());
        REQUIRE(d.root().size() == 1);
    }
    Config::instance().set_max_depth(guard.m_max_depth);

    // strip_bom: stream content delegates to parse_in_situ
    const std::string bom = "\xEF\xBB\xBF{\"a\":1}";
    {
        std::istringstream bom_in(bom);
        REQUIRE_THROWS_AS((void)parse_from_istream(bom_in), ParseError);
    }
    Config::instance().set_strip_bom(true);
    {
        std::istringstream in(bom);
        Document d = parse_from_istream(in);
        REQUIRE(d.root()["a"] == (int64_t)1);
    }
    Config::instance().set_strip_bom(false);

    // strict_utf8: first offending byte sits at index 2; offsets stay
    // relative to the stream start
    const std::string bad_utf8 = "\"a\xFF" "b\"";
    {
        std::istringstream in(bad_utf8);
        Document d = parse_from_istream(in); // default: byte mirror
        REQUIRE(d.root().is_string());
        REQUIRE(d.root().as_string() ==
                std::string_view(bad_utf8.data() + 1, 3));
    }
    Config::instance().set_strict_utf8(true);
    try {
        std::istringstream bad_in(bad_utf8);
        (void)parse_from_istream(bad_in);
        REQUIRE(false);
    } catch (const ParseError &e) {
        REQUIRE(e.offset() == 2);
    }
    Config::instance().set_strict_utf8(false);
}

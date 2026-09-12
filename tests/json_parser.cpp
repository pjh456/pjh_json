#include <doctest/doctest.h>
#include <ostream>
#include <string_view>
#include <cstring>
#include <stdexcept>
#include <cmath>
#include <limits>

#include <pjh_json/document.hpp>
#include <pjh_json/parser.hpp>
#include <pjh_json/writer.hpp>

using namespace pjh::json;

namespace
{
    // doctest has no per-case setup/teardown: a case that mutates the
    // global Config singleton must restore it itself. RAII (not a manual
    // end-of-case restore) so the restore still runs when a REQUIRE fails
    // and unwinds the case. Covers the knobs with getter+setter pairs;
    // storage/block are restored manually by their cases.
    struct ConfigGuard
    {
        bool m_strict;
        size_t m_arena_block;
        size_t m_max_depth;
        bool m_strip_bom;
        bool m_strict_utf8;
        bool m_json5;

        ConfigGuard()
            : m_strict(Config::instance().strict_duplicate_keys()),
              m_arena_block(Config::instance().arena_block_size()),
              m_max_depth(Config::instance().max_depth()),
              m_strip_bom(Config::instance().strip_bom()),
              m_strict_utf8(Config::instance().strict_utf8()),
              m_json5(Config::instance().json5())
        {
        }

        ~ConfigGuard()
        {
            Config::instance().set_strict_duplicate_keys(m_strict);
            Config::instance().set_arena_block_size(m_arena_block);
            Config::instance().set_max_depth(m_max_depth);
            Config::instance().set_strip_bom(m_strip_bom);
            Config::instance().set_strict_utf8(m_strict_utf8);
            Config::instance().set_json5(m_json5);
        }
    };

    // UTF-8 BOM bytes (hex escapes are self-terminating: \xBF does not
    // swallow a following byte)
    inline std::string bom() { return std::string("\xEF\xBB\xBF", 3); }

    // Ill-formed UTF-8 samples (hex escapes self-terminating: \xBF does
    // not swallow the following byte)
    inline std::string invalid_byte()  { return std::string("a\xFF", 2); }
    inline std::string lone_cont()     { return std::string("a\x80", 2); }
    inline std::string overlong2()     { return std::string("a\xC0\x80", 3); }
    inline std::string overlong3()     { return std::string("a\xE0\x80\x80", 4); }
    inline std::string surrogate_seq() { return std::string("a\xED\xA0\x80", 4); }
    inline std::string over_10ffff()   { return std::string("a\xF5\x80\x80\x80", 5); }
    inline std::string truncated_tail(){ return std::string("a\xE0", 2); }
    inline std::string valid_utf8()    { return std::string("caf\xC3\xA9 \xE4\xB8\xAD \xF0\x9F\x98\x80"); }
}

TEST_CASE("Parser: literal") {
    auto doc1 = parse_copy("null");
    REQUIRE(doc1.root().is_null());

    auto doc2 = parse_copy("true");
    REQUIRE(doc2.root().is_boolean());
    REQUIRE(doc2.root().as_boolean() == true);

    auto doc3 = parse_copy("false");
    REQUIRE(doc3.root().is_boolean());
    REQUIRE(doc3.root().as_boolean() == false);

    auto doc4 = parse_copy(" \t\r\n   true \n\t ");
    REQUIRE(doc4.root().is_boolean());
    REQUIRE(doc4.root().as_boolean() == true);
}

TEST_CASE("Parser: number") {
    auto doc1 = parse_copy("42");
    REQUIRE(doc1.root().as_int() == (int64_t)42);

    auto doc2 = parse_copy("-12345");
    REQUIRE(doc2.root().as_int() == (int64_t)-12345);

    auto doc3 = parse_copy("3.14159");
    REQUIRE(doc3.root().is_float());
    REQUIRE(std::abs(doc3.root().as_float() - 3.14159) < 1e-6);

    auto doc4 = parse_copy("-0.05e2");
    REQUIRE(doc4.root().is_float());
    REQUIRE(std::abs(doc4.root().as_float() - (-5.0)) < 1e-6);
}

TEST_CASE("Parser: string") {
    auto doc1 = parse_copy("\"hello world\"");
    REQUIRE(doc1.root().as_string() == "hello world");

    auto doc2 = parse_copy("\"line1\\nline2\\t\\\"quote\\\"\"");
    REQUIRE(doc2.root().as_string() == "line1\nline2\t\"quote\"");

    auto doc3 = parse_copy("\"\\uD83D\\uDE00\"");
    REQUIRE(doc3.root().as_string() == "\xF0\x9F\x98\x80");

    auto doc4 = parse_copy("\"this is a very long string without any escape characters to test simd acceleration speed\"");
    REQUIRE(doc4.root().as_string() == "this is a very long string without any escape characters to test simd acceleration speed");
}

TEST_CASE("Parser: array") {
    auto doc1 = parse_copy("[]");
    REQUIRE(doc1.root().is_array());
    REQUIRE(doc1.root().size() == 0);

    auto doc2 = parse_copy("[1, 2, 3]");
    REQUIRE(doc2.root().is_array());
    REQUIRE(doc2.root().size() == 3);
    REQUIRE(doc2.root()[0] == (int64_t)1);
    REQUIRE(doc2.root()[1] == (int64_t)2);
    REQUIRE(doc2.root()[2] == (int64_t)3);

    auto doc3 = parse_copy("[[1, 2], [3, 4]]");
    REQUIRE(doc3.root().is_array());
    REQUIRE(doc3.root().size() == 2);
    REQUIRE(doc3.root()[0][1] == (int64_t)2);
    REQUIRE(doc3.root()[1][0] == (int64_t)3);
}

TEST_CASE("Parser: object") {
    auto doc1 = parse_copy("{}");
    REQUIRE(doc1.root().is_object());
    REQUIRE(doc1.root().size() == 0);

    auto doc2 = parse_copy("{\"name\": \"pjh\", \"version\": 1}");
    REQUIRE(doc2.root().is_object());
    REQUIRE(doc2.root().size() == 2);
    REQUIRE(doc2.root()["name"] == "pjh");
    REQUIRE(doc2.root()["version"] == (int64_t)1);

    auto doc3 = parse_copy("{\"user\": {\"id\": 100, \"active\": true}}");
    REQUIRE(doc3.root()["user"].is_object());
    REQUIRE(doc3.root()["user"]["id"] == (int64_t)100);
    REQUIRE(doc3.root()["user"]["active"] == true);
}

TEST_CASE("Parser: complex") {
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

    REQUIRE(doc.root().is_object());
    REQUIRE(doc.root()["project"] == "pjh_json");
    REQUIRE(doc.root()["is_fast"] == true);

    REQUIRE(doc.root()["features"].is_array());
    REQUIRE(doc.root()["features"].size() == 3);
    REQUIRE(doc.root()["features"][0] == "simd");

    REQUIRE(doc.root()["metadata"].is_object());
    REQUIRE(doc.root()["metadata"]["version"].is_float());
    REQUIRE(doc.root()["metadata"]["author"].is_null());
}

TEST_CASE("Parser: error handling") {
    ConfigGuard guard; // first: save the entering state before any mutation
    Config::instance().set_strict_duplicate_keys(true);

    CHECK_THROWS_AS((void)parse_copy(""), std::runtime_error);
    CHECK_THROWS_AS((void)parse_copy("{"), std::runtime_error);
    CHECK_THROWS_AS((void)parse_copy("["), std::runtime_error);
    CHECK_THROWS_AS((void)parse_copy("\"unterminated"), std::runtime_error);
    CHECK_THROWS_AS((void)parse_copy("{\"key\": }"), std::runtime_error);
    CHECK_THROWS_AS((void)parse_copy("[1, 2,]"), std::runtime_error);
    CHECK_THROWS_AS((void)parse_copy("true false"), std::runtime_error);
    CHECK_THROWS_AS((void)parse_copy("01"), std::runtime_error);
    CHECK_THROWS_AS((void)parse_copy("1."), std::runtime_error);
    CHECK_THROWS_AS((void)parse_copy("1e"), std::runtime_error);
    CHECK_THROWS_AS((void)parse_copy("1e+"), std::runtime_error);
    CHECK_THROWS_AS((void)parse_copy("-"), std::runtime_error);
    CHECK_THROWS_AS((void)parse_copy("-.1"), std::runtime_error);
    CHECK_THROWS_AS((void)parse_copy("{\"a\":1,\"a\":2}"), std::runtime_error);
}

TEST_CASE("Parser: empty input null data") {
    // Default string_view: data()==nullptr, size()==0. parse_copy must guard
    // the memcpy (memcpy(dst, null, 0) is UB even for n==0); the parse then
    // fails as empty input at offset 0.
    try {
        (void)parse_copy(std::string_view{});
        REQUIRE(false);
    } catch (const ParseError &e) {
        REQUIRE(e.offset() == 0);
        REQUIRE(std::string(e.what()).find("Unexpected end of input")
                != std::string::npos);
    }

    // Explicit (nullptr, 0) spelling: same guard, same result.
    CHECK_THROWS_AS((void)parse_copy(std::string_view(nullptr, 0)), ParseError);
    CHECK_THROWS_AS((void)parse_copy_result(std::string_view{}).unwrap(),
                    pjh::result::bad_result_access); // control: Result carries Err

    // parse_jsonl: empty input is a successful empty array (no memcpy from null).
    auto d = parse_jsonl(std::string_view{});
    REQUIRE(d.root().is_array());
    REQUIRE(d.root().size() == 0);
}

TEST_CASE("Parser: unicode escape error offset") {
    auto expect_hex = [](std::string_view input, size_t off) {
        try {
            (void)parse_copy(input);
            REQUIRE(false);
        } catch (const ParseError &e) {
            REQUIRE(e.offset() == off);
            REQUIRE(std::string(e.what()).find("Invalid hex digit in unicode escape")
                    != std::string::npos);
            REQUIRE(std::string(e.what()).find(" at offset " + std::to_string(off))
                    != std::string::npos);
        }
    };

    // Offending digit index (0 = opening quote):
    expect_hex(R"("\uZZZZ")",       3); // first hex digit bad
    expect_hex(R"("\uD8ZZ")",       5); // third hex digit bad
    expect_hex(R"("\u12")",         5); // closing quote reached early
    expect_hex(R"("\uD800\uZZZZ")", 9); // second unit's first digit bad

    // Valid pair / valid escapes still parse (cursor semantics unchanged).
    REQUIRE(parse_copy(R"("\uD83D\uDE00")").root().as_string()
            == std::string_view("\xF0\x9F\x98\x80"));

    // Surrogate semantic errors keep their established offsets (control,
    // already pinned in "Parser: strict utf-8 rejects malformed bytes" == 7).
    try {
        (void)parse_copy(R"("\uD800")");
        REQUIRE(false);
    } catch (const ParseError &e) {
        REQUIRE(e.offset() == 7);
    }
}

TEST_CASE("Parser: strict strings and escapes") {
    // Reference-side pins, dual to the consteval static_asserts in
    // tests/literal_test.cpp "ConstJson: parse strictness"
    CHECK_THROWS_AS((void)parse_copy("\"\\q\""), ParseError);
    CHECK_THROWS_AS((void)parse_copy("\"\\uD800\""), ParseError);
    CHECK_THROWS_AS((void)parse_copy("\"\\uDC00\""), ParseError);
    CHECK_THROWS_AS((void)parse_copy("\"\\uD800A\""), ParseError);
    CHECK_THROWS_AS((void)parse_copy("\"\\u12\""), ParseError);
    CHECK_THROWS_AS((void)parse_copy("\"a\nb\""), ParseError);            // raw LF
    CHECK_THROWS_AS((void)parse_copy(std::string("\"a\x01" "b\"")), ParseError);
    CHECK_THROWS_AS((void)parse_copy("00"), ParseError);                   // leading zeros
    auto ok1 = parse_copy("\"\\uD83D\\uDE00\"");
    REQUIRE(ok1.root().as_string() == "\xF0\x9F\x98\x80");
    auto ok2 = parse_copy("0.5");
    REQUIRE(ok2.root().is_float());
    auto ok3 = parse_copy(std::string("\"a\x7f" "b\""));
    REQUIRE(ok3.root().as_string() == std::string_view("a\x7f" "b"));
}

TEST_CASE("Parser: view") {
    std::string content = R"({"name": "pjh", "items": [1, 2, 3], "active": true})";
    std::string buf(content.size() + kPaddingWidth, '\0');
    memcpy(buf.data(), content.data(), content.size());

    auto doc = parse_view(buf.data(), content.size());
    REQUIRE(doc.is_view());
    REQUIRE(doc.root().is_object());
    REQUIRE(doc.root()["name"] == "pjh");
    REQUIRE(doc.root()["active"] == true);
    REQUIRE(doc.root()["items"].is_array());
    REQUIRE(doc.root()["items"].size() == 3);

    std::string esc_content = R"({"msg": "hello\nworld"})";
    std::string esc_buf(esc_content.size() + kPaddingWidth, '\0');
    memcpy(esc_buf.data(), esc_content.data(), esc_content.size());

    auto doc2 = parse_view(esc_buf.data(), esc_content.size());
    REQUIRE(doc2.is_view());
    REQUIRE(doc2.root().is_object());
    REQUIRE(doc2.root()["msg"] == "hello\nworld");
}

TEST_CASE("Parser: whitespace rejects C0 control bytes") {
    // RFC 8259 §2: whitespace is exactly space (0x20), tab (0x09),
    // LF (0x0A), CR (0x0D). Every other C0 control byte is illegal in
    // both value-before and value-after positions.
    for (int c = 1; c < 0x20; ++c)
    {
        if (c == 0x09 || c == 0x0A || c == 0x0D)
            continue;
        std::string lead = std::string(1, static_cast<char>(c)) + "1";
        CHECK_THROWS_AS((void)parse_copy(lead), std::runtime_error);
        std::string trail = "1";
        trail += static_cast<char>(c);
        CHECK_THROWS_AS((void)parse_copy(trail), std::runtime_error);
    }
    // container-internal positions (object key->value gap, array element gap)
    std::string obj = std::string("{\"k\" ") + static_cast<char>(0x0B) + " : 1}";
    CHECK_THROWS_AS((void)parse_copy(obj), std::runtime_error);
    std::string arr = std::string("[1 ") + static_cast<char>(0x1B) + " , 2]";
    CHECK_THROWS_AS((void)parse_copy(arr), std::runtime_error);
}

TEST_CASE("Parser: NUL and DEL are not whitespace") {
    CHECK_THROWS_AS((void)parse_copy(std::string(1, '\0') + "1"), std::runtime_error);
    CHECK_THROWS_AS((void)parse_copy(std::string("1") + std::string(1, '\0')), std::runtime_error);
    CHECK_THROWS_AS((void)parse_copy(std::string(1, char(0x7F))), std::runtime_error);
}

TEST_CASE("Parser: jsonl control byte line rejected") {
    // A line made of an illegal control byte must error, not silently hop
    // into the next line and duplicate its value.
    CHECK_THROWS_AS(
        (void)parse_jsonl(std::string(1, char(0x0B)) + "\n[1]\n"),
        std::runtime_error);
    CHECK_THROWS_AS(
        (void)parse_jsonl(std::string(1, '\0') + "\n[1]\n"),
        std::runtime_error);
    // A whitespace-only line (CR/space) is blank: skipped, not duplicated.
    auto doc_ws = parse_jsonl(std::string("\r \r\n[1]\n"));
    REQUIRE(doc_ws.root().is_array());
    REQUIRE(doc_ws.root().size() == 1);
    REQUIRE(doc_ws.root()[0][0] == (int64_t)1);
    // Legal jsonl still parses.
    auto doc = parse_jsonl("1\n2\n");
    REQUIRE(doc.root().is_array());
    REQUIRE(doc.root().size() == 2);
    REQUIRE(doc.root()[0] == (int64_t)1);
    REQUIRE(doc.root()[1] == (int64_t)2);
}

TEST_CASE("Parser: max depth") {
    ConfigGuard guard; // RAII: restores entering Config state, even on throw

    auto deep = [](size_t n, char o, char c)
    {
        return std::string(n, o) + std::string(n, c);
    };
    // Number of nested arrays along the single-element chain (empty root = 0)
    auto depth_of = [](const Json &j)
    {
        size_t d = 0;
        const Json *cur = &j;
        while (cur->is_array())
        {
            ++d;
            if (cur->empty())
                break;
            REQUIRE(cur->size() == 1);
            cur = &(*cur)[0];
        }
        return d;
    };

    // Default: finite DoS guard. Exactly kDefaultMaxDepth passes, one more
    // throws a located ParseError (secure by default).
    REQUIRE(Config::instance().max_depth() == Config::kDefaultMaxDepth);
    auto doc_default = parse_copy(deep(Config::kDefaultMaxDepth, '[', ']'));
    REQUIRE(depth_of(doc_default.root()) == Config::kDefaultMaxDepth);
    CHECK_THROWS_AS((void)parse_copy(deep(Config::kDefaultMaxDepth + 1, '[', ']')),
                    ParseError);
    CHECK_THROWS_WITH((void)parse_copy(deep(Config::kDefaultMaxDepth + 1, '[', ']')),
                      "Maximum nesting depth exceeded at offset 512");

    // Explicit opt-out: 0 = unlimited, now requiring an explicit setter call.
    // The input exceeds the default bound, proving the limit is lifted.
    Config::instance().set_max_depth(Config::kUnlimitedDepth);
    auto doc_unlimited = parse_copy(deep(Config::kDefaultMaxDepth + 100, '[', ']'));
    REQUIRE(depth_of(doc_unlimited.root()) == Config::kDefaultMaxDepth + 100);

    // Exactly N passes, N+1 throws
    Config::instance().set_max_depth(10);
    auto doc10 = parse_copy(deep(10, '[', ']'));
    REQUIRE(depth_of(doc10.root()) == 10);
    CHECK_THROWS_AS((void)parse_copy(deep(11, '[', ']')), ParseError);
    CHECK_THROWS_WITH((void)parse_copy(deep(11, '[', ']')),
                      "Maximum nesting depth exceeded at offset 10");

    // Object path counts symmetrically with arrays
    Config::instance().set_max_depth(2);
    auto doc2 = parse_copy("{\"a\":{\"b\":1}}");
    REQUIRE(doc2.root().is_object());
    CHECK_THROWS_AS((void)parse_copy("{\"a\":{\"b\":{}}"), ParseError);

    // Sibling containers do not add depth: three objects share level 2
    Config::instance().set_max_depth(2);
    auto doc1 = parse_copy("[{},{},{}]");
    REQUIRE(doc1.root().is_array());
    REQUIRE(doc1.root().size() == 3);
    // but wrapping them in another array adds a level
    CHECK_THROWS_AS((void)parse_copy("[[{}]]"), ParseError);

    // JSONL: depth resets per line, does not accumulate across lines
    Config::instance().set_max_depth(2);
    auto jl = parse_jsonl("[[1]]\n[[1]]\n");
    REQUIRE(jl.root().is_array());
    REQUIRE(jl.root().size() == 2);
    CHECK_THROWS_AS((void)parse_jsonl("[[[1]]]\n"), ParseError);
}

TEST_CASE("Parser: 19-digit integer boundary") {
    // 19-digit ints within [INT64_MIN, INT64_MAX] must stay int64, exact
    auto d1 = parse_copy("9223372036854775807"); // INT64_MAX (was double, +1 drift)
    REQUIRE(d1.root().is_int());
    REQUIRE(d1.root().as_int() == std::numeric_limits<int64_t>::max());

    auto d2 = parse_copy("9223372036854775806"); // INT64_MAX - 1 (was double, +2 drift)
    REQUIRE(d2.root().is_int());
    REQUIRE(d2.root().as_int() == (int64_t)9223372036854775806);

    auto d3 = parse_copy("1000000000000000000"); // 10^18, smallest 19-digit int
    REQUIRE(d3.root().is_int());
    REQUIRE(d3.root().as_int() == (int64_t)1000000000000000000);

    // Regression: <= 18 digits unchanged
    auto d4 = parse_copy("999999999999999999");
    REQUIRE(d4.root().is_int());
    REQUIRE(d4.root().as_int() == (int64_t)999999999999999999);
    auto d5 = parse_copy("-999999999999999999");
    REQUIRE(d5.root().is_int());
    REQUIRE(d5.root().as_int() == (int64_t)-999999999999999999);

    // INT64_MIN: magnitude exactly 2^63 (negation-overflow special case)
    auto d6 = parse_copy("-9223372036854775808");
    REQUIRE(d6.root().is_int());
    REQUIRE(d6.root().as_int() == std::numeric_limits<int64_t>::min());

    auto d7 = parse_copy("-9223372036854775807"); // INT64_MIN + 1 (was -1 drift)
    REQUIRE(d7.root().is_int());
    REQUIRE(d7.root().as_int() == (int64_t)-9223372036854775807);

    // 19-digit ints outside int64 range fall to double
    auto d8 = parse_copy("9223372036854775808"); // 2^63, exact in double
    REQUIRE(d8.root().is_float());
    REQUIRE(d8.root().as_float() == 9223372036854775808.0);

    auto d9 = parse_copy("9999999999999999999"); // rounds to 1e19, exact in double
    REQUIRE(d9.root().is_float());
    REQUIRE(d9.root().as_float() == 1e19);

    auto d10 = parse_copy("-9223372036854775809"); // -2^63-1, rounds to -2^63
    REQUIRE(d10.root().is_float());
    REQUIRE(d10.root().as_float() == -9223372036854775808.0);

    auto d11 = parse_copy("18446744073709551615"); // UINT64_MAX, 20 digits -> 2^64.0
    REQUIRE(d11.root().is_float());
    REQUIRE(d11.root().as_float() == 18446744073709551616.0);

    // Float indicators still force double
    auto d12 = parse_copy("9223372036854775807.5");
    REQUIRE(d12.root().is_float());
    auto d13 = parse_copy("9223372036854775807e0");
    REQUIRE(d13.root().is_float());

    // -0 stays int 0
    auto d14 = parse_copy("-0");
    REQUIRE(d14.root().is_int());
    REQUIRE(d14.root().as_int() == (int64_t)0);

    // JSONL: same parse_number path per line
    auto jl = parse_jsonl("9223372036854775807\n9223372036854775808\n");
    REQUIRE(jl.root().is_array());
    REQUIRE(jl.root().size() == 2);
    REQUIRE(jl.root()[0].is_int());
    REQUIRE(jl.root()[0].as_int() == std::numeric_limits<int64_t>::max());
    REQUIRE(jl.root()[1].is_float());
}

TEST_CASE("Parser: out-of-double-range numbers") {
    // RFC 8259 §6 lets implementations bound the accepted range; this
    // library's bound is a finite double. A grammar-legal token whose value
    // is not representable as a finite double is a RANGE error positioned at
    // the token start (including the optional '-'), not a format error.
    auto expect_range = [](std::string_view input, size_t off) {
        try {
            (void)parse_copy(input);
            REQUIRE(false);
        } catch (const ParseError &e) {
            REQUIRE(e.category() == Category::Parse);
            REQUIRE(e.offset() == off);
            REQUIRE(std::string(e.what()).find("Number out of double range")
                    != std::string::npos);
            REQUIRE(std::string(e.what()).find("Invalid number")
                    == std::string::npos);
        }
    };

    // Overflow to infinity
    expect_range("1e400", 0);
    expect_range("1E400", 0);
    expect_range("1e+400", 0);
    expect_range("-1e400", 0);
    expect_range("1e309", 0);
    expect_range("1.8e308", 0);

    // Underflow to zero
    expect_range("1e-400", 0);
    expect_range("-1e-400", 0);

    // 10^400: '1' followed by 400 zeros (pure integer, no float indicator)
    {
        std::string huge(1, '1');
        huge.append(400, '0');
        expect_range(huge, 0);
    }

    // The anchor is the token start, not the container or the token end
    expect_range("[1e400]", 1);
    expect_range("{\"a\":1e400}", 5);

    // *_result shells inherit the positioned range error (task 16 channel)
    auto r = parse_copy_result("1e400");
    REQUIRE(r.is_err());
    ParseError e = r.unwrap_err();
    REQUIRE(e.offset() == 0);
    REQUIRE(e.category() == Category::Parse);
    REQUIRE(dynamic_cast<const ParseError *>(&e) != nullptr);
    REQUIRE(std::string(e.what()).find("out of double range")
            != std::string::npos);

    // parse_jsonl: offsets are line-relative (m_begin = line base)
    {
        auto jr = parse_jsonl_result("1e308\n1e400\n");
        REQUIRE(jr.is_err());
        ParseError je = jr.unwrap_err();
        REQUIRE(je.offset() == 0); // second line starts at line offset 0
        REQUIRE(je.category() == Category::Parse);
    }
    {
        auto jr = parse_jsonl_result("1e400\n");
        REQUIRE(jr.is_err());
        REQUIRE(jr.unwrap_err().offset() == 0);
    }

    // Finite regression: none of these may be misclassified as out of range.
    auto f1 = parse_copy("1e308");
    REQUIRE(f1.root().is_float());
    REQUIRE(f1.root().as_float() == 1e308);

    auto f2 = parse_copy("1.7976931348623157e308"); // DBL_MAX
    REQUIRE(f2.root().is_float());
    REQUIRE(f2.root().as_float() == std::numeric_limits<double>::max());

    auto f3 = parse_copy("18446744073709551615"); // UINT64_MAX -> 2^64.0
    REQUIRE(f3.root().is_float());
    REQUIRE(f3.root().as_float() == 18446744073709551616.0);

    auto f4 = parse_copy("9999999999999999999"); // rounds to 1e19
    REQUIRE(f4.root().is_float());
    REQUIRE(f4.root().as_float() == 1e19);

    auto f5 = parse_copy("9223372036854775808"); // 2^63.0
    REQUIRE(f5.root().is_float());
    REQUIRE(f5.root().as_float() == 9223372036854775808.0);

    auto f6 = parse_copy("1e-320"); // subnormal
    REQUIRE(f6.root().is_float());
    REQUIRE(f6.root().as_float() == 1e-320);

    auto f7 = parse_copy("5e-324"); // smallest subnormal
    REQUIRE(f7.root().is_float());
    REQUIRE(f7.root().as_float() == 5e-324);

    auto f8 = parse_copy("0e400"); // zero value, huge exponent
    REQUIRE(f8.root().is_float());
    REQUIRE(f8.root().as_float() == 0.0);

    auto f9 = parse_copy("-0e400"); // negative zero is representable
    REQUIRE(f9.root().is_float());
    REQUIRE(f9.root().as_float() == 0.0);

    // Boundary pairs: finite vs rejected (no ambiguous +/-0.5 ulp cases)
    REQUIRE(parse_copy("1e308").root().is_float());
    expect_range("1e309", 0);
    REQUIRE(parse_copy("0e400").root().as_float() == 0.0);
    expect_range("1e-400", 0);
}

TEST_CASE("Parser: duplicate keys last-wins") {
    // strict off (default): last-wins, matching Object::insert
    auto d1 = parse_copy(R"({"a":1,"a":2})");
    REQUIRE(d1.root().is_object());
    REQUIRE(d1.root().size() == 1);
    REQUIRE(d1.root()["a"] == (int64_t)2);

    // in-place overwrite: first position and key preserved
    auto d2 = parse_copy(R"({"a":1,"b":2,"a":3})");
    REQUIRE(d2.root().size() == 2);
    REQUIRE(d2.root().as_object().begin()->first == "a");
    REQUIRE(d2.root()["b"] == (int64_t)2);
    REQUIRE(d2.root()["a"] == (int64_t)3);

    // container value replaced in place (old object destroyed mid-parse)
    auto d3 = parse_copy(R"({"k":{"x":1},"k":[1,2]})");
    REQUIRE(d3.root()["k"].is_array());
    REQUIRE(d3.root()["k"].size() == 2);

    // triple duplicate collapses to the last value
    auto d4 = parse_copy(R"({"a":1,"a":2,"a":3})");
    REQUIRE(d4.root().size() == 1);
    REQUIRE(d4.root()["a"] == (int64_t)3);

    // jsonl shares the same object path
    auto jl = parse_jsonl("{\"a\":1,\"a\":2}\n");
    REQUIRE(jl.root().is_array());
    REQUIRE(jl.root().size() == 1);
    REQUIRE(jl.root()[0]["a"] == (int64_t)2);

    // round trip: dump emits the single last-wins entry
    auto out = dump(d1.root());
    REQUIRE(out == R"({"a":2})");
}

TEST_CASE("Parser: duplicate keys large object index path") {
    // An object past the internal index threshold switches the non-strict
    // first-match scan from a scalar sweep to a key->position index. This
    // pins that the indexed path is semantically identical: same size,
    // same first key/position and insertion order, same last-wins values.
    constexpr int n = 300; // > kKeyIndexThreshold (256)
    std::string json = "{";
    for (int i = 0; i < n; ++i)
    {
        if (i)
            json += ',';
        json += "\"k" + std::to_string(i) + "\":1";
    }
    // duplicates appended after the index is already active
    for (int i = 0; i < 8; ++i)
        json += ",\"k" + std::to_string(i * 7) + "\":2";
    json += '}';

    auto d = parse_copy(json);
    REQUIRE(d.root().is_object());
    REQUIRE(d.root().size() == (size_t)n);
    // first occurrence keeps its key and position
    REQUIRE(d.root().as_object().begin()->first == "k0");
    // insertion order is the first-occurrence order
    int seen = 0;
    for (const auto &entry : d.root().as_object())
    {
        REQUIRE(entry.first == ("k" + std::to_string(seen)));
        ++seen;
    }
    REQUIRE(seen == n);
    // last value wins for duplicated keys, first value elsewhere
    for (int k = 0; k < n; ++k)
    {
        bool is_dup = (k < 56 && k % 7 == 0);
        auto key = "k" + std::to_string(k);
        REQUIRE(d.root()[key] == (int64_t)(is_dup ? 2 : 1));
    }

    // Escaped keys materialise as owned pmr::strings: the index compares
    // through the live entry keys, so this must hold after the key String
    // is moved into the entry (and after in-place overwrites).
    std::string esc = "{";
    for (int k = 0; k < n; ++k)
    {
        if (k)
            esc += ',';
        esc += "\"\\u006b" + std::to_string(k) + "\":1";
    }
    esc += ",\"\\u006b3\":2}";
    auto de = parse_copy(esc);
    REQUIRE(de.root().size() == (size_t)n);
    REQUIRE(de.root()["k3"] == (int64_t)2);
    REQUIRE(de.root()["k0"] == (int64_t)1);
}

TEST_CASE("Parser: strict duplicate keys large object") {
    ConfigGuard guard; // first: save the entering state before any mutation
    Config::instance().set_strict_duplicate_keys(true);

    // A large object (past kKeyIndexThreshold) with all-distinct keys must
    // parse: strict mode never builds a key index, every key is appended
    // directly, and insertion order/values are preserved.
    constexpr int n = 300; // > kKeyIndexThreshold (256)
    std::string json = "{";
    for (int i = 0; i < n; ++i)
    {
        if (i)
            json += ',';
        json += "\"k" + std::to_string(i) + "\":1";
    }
    json += '}';

    auto d = parse_copy(json);
    REQUIRE(d.root().is_object());
    REQUIRE(d.root().size() == (size_t)n);
    int seen = 0;
    for (const auto &entry : d.root().as_object())
    {
        REQUIRE(entry.first == ("k" + std::to_string(seen)));
        REQUIRE(entry.second == (int64_t)1);
        ++seen;
    }
    REQUIRE(seen == n);

    // A duplicate after the threshold must still throw: skipping the
    // first-match scan must not skip the duplicate verdict.
    std::string dup = json.substr(0, json.size() - 1) + ",\"k7\":2}";
    REQUIRE_THROWS_AS((void)parse_copy(dup), ParseError);

    // An escaped key equal to a plain key still collides.
    REQUIRE_THROWS_AS((void)parse_copy(R"({"\u0061":1,"a":2})"), ParseError);

    // Escaped large object, all distinct, still succeeds (the borrowed
    // decoded key views stay stable in the strict seen set).
    std::string esc = "{";
    for (int k = 0; k < n; ++k)
    {
        if (k)
            esc += ',';
        esc += "\"\\u006b" + std::to_string(k) + "\":1";
    }
    esc += '}';
    auto de = parse_copy(esc);
    REQUIRE(de.root().size() == (size_t)n);

    // Empty-string key duplicated still throws, and the empty detail must
    // substitute into the message (not leave a literal "{}").
    try {
        (void)parse_copy(R"({"":1,"":2})");
        REQUIRE(false);
    } catch (const ParseError &e) {
        REQUIRE(e.offset() == 0);
        REQUIRE(std::string(e.what()) == "Duplicate key \"\" in object");
    }
}

TEST_CASE("Parser: in_situ padding") {
    using std::pmr::get_default_resource;

    auto make_padded = [](char tail)
    {
        std::pmr::string buf(get_default_resource());
        buf.assign(R"({"a":1})");
        buf.append(kPaddingWidth - 1, '\0');
        buf.push_back(tail); // '\0' = compliant; 'x' = contract broken
        return buf;
    };

    // Positive case: compliant padding parses (parse_file takes the same
    // path: resize(n, '\0') zero-fills the tail, so the new check must pass)
    auto doc = parse_in_situ(make_padded('\0'));
    REQUIRE(doc.root()["a"] == (int64_t)1);

    // size < kPaddingWidth rejected (pre-existing check, pinned)
    std::pmr::string small(get_default_resource());
    small.assign("{}", 2);
    small.append(60, '\0'); // total 62
    CHECK_THROWS_AS((void)parse_in_situ(std::move(small)), ParseError);

    // Non-NUL tail rejected (NEW check: pre-fix this parsed silently)
    CHECK_THROWS_AS((void)parse_in_situ(make_padded('x')), ParseError);
    #ifndef NDEBUG
    CHECK_THROWS_WITH((void)parse_in_situ(make_padded('x')),
                      "In-situ buffer padding must be NUL bytes");
    #endif

    // Behavioral minimum-width pin: a buffer padded with exactly the
    // contract width (kPaddingWidth NUL bytes) must parse. A narrower
    // contract leaves NULs inside the content range and trips "Extra
    // characters after complete JSON value".
    std::pmr::string isitu_buf(get_default_resource());
    isitu_buf.assign(R"({"a":1})");
    isitu_buf.append(kPaddingWidth, '\0');
    auto isitu_doc = parse_in_situ(std::move(isitu_buf));
    REQUIRE(isitu_doc.root()["a"] == (int64_t)1);
}

TEST_CASE("Parser: padding width contract") {
    // Fixed padding contract: kPaddingWidth is ISA-independent, pinned so a
    // change must update this test and the docs in lockstep.
    static_assert(kPaddingWidth == 128,
                  "fixed padding = 2x the maximum supported uint8 batch (64)");
    // Behavioral side: a view with exactly kPaddingWidth padding works
    // (regression pin)
    std::string content = R"([1,2,3])";
    std::string buf(content.size() + kPaddingWidth, '\0');
    memcpy(buf.data(), content.data(), content.size());
    auto doc = parse_view(buf.data(), content.size());
    REQUIRE(doc.root().size() == 3);
}

TEST_CASE("Parser: result entry success") {
    auto r = parse_copy_result(R"({"a":1,"b":[true,null]})");
    REQUIRE(r.is_ok());
    Document d = std::move(r).unwrap();
    REQUIRE(d.root()["a"].is_int());
    REQUIRE(d.root()["b"].is_array());
    // unwrap() moves the Document out of the channel; the Result enters the
    // Moved state (see the moved-state case), pins land on the local
    REQUIRE(d.root()["b"].size() == 2);
}

TEST_CASE("Parser: result entry positioned error") {
    auto r = parse_copy_result("{"); // "Expected string key in object"
    REQUIRE(r.is_err());
    ParseError e = r.unwrap_err();
    REQUIRE(e.offset() == 1); // '{' consumed, cursor at padding NUL
    REQUIRE(e.category() == Category::Parse);
    // no-slicing pin: the channel still holds the full ParseError (dynamic
    // type + machine channel survive the by-value copy; what() carries the
    // offset segment, task 13 shape, mode-independent)
    REQUIRE(dynamic_cast<const ParseError *>(&e) != nullptr);
    REQUIRE(std::string(e.what()).find(" at offset 1") != std::string::npos);
}

TEST_CASE("Parser: result entry context-free error") {
    ConfigGuard guard; // first: save the entering state before any mutation
    Config::instance().set_strict_duplicate_keys(true);

    // in_situ size below kPaddingWidth (offset 0, context-free)
    std::pmr::string small;
    small.resize(10, '\0');
    auto r1 = parse_in_situ_result(std::move(small));
    REQUIRE(r1.is_err());
    ParseError e1 = r1.unwrap_err();
    REQUIRE(e1.offset() == 0);
    REQUIRE(e1.category() == Category::Parse);

    // strict duplicate key (offset 0, context-free)
    auto r2 = parse_copy_result(R"({"a":1,"a":2})");
    REQUIRE(r2.is_err());
    ParseError e2 = r2.unwrap_err();
    REQUIRE(e2.offset() == 0);
    REQUIRE(e2.category() == Category::Parse);
}

TEST_CASE("Parser: jsonl result entry") {
    // Line 2 fails: offset is RELATIVE TO THE LINE, not the whole input
    // ('x' sits at whole-input offset 7 but line offset 3)
    auto r = parse_jsonl_result("[1]\n[2 x]\n[3]");
    REQUIRE(r.is_err());
    ParseError e = r.unwrap_err();
    REQUIRE(e.category() == Category::Parse);
    REQUIRE(e.offset() == 3);
    REQUIRE(dynamic_cast<const ParseError *>(&e) != nullptr);

    // All lines legal -> one Array with one element per line
    auto ok = parse_jsonl_result("[1]\n[2]\n[3]");
    REQUIRE(ok.is_ok());
    Document d = std::move(ok).unwrap();
    REQUIRE(d.root().as_array().size() == 3);
}

TEST_CASE("Parser: view result entry") {
    // The result form never runtime-verifies caller padding (unlike
    // parse_in_situ_result); the buffer is the contract-legal padded form,
    // with the JSON itself truncated at the content end
    std::string content = R"({"a":1})";
    std::string buf(content.size() + kPaddingWidth, '\0');
    memcpy(buf.data(), content.data(), content.size());
    auto r = parse_view_result(buf.data(), content.size());
    REQUIRE(r.is_ok());
    Document d = std::move(r).unwrap();
    REQUIRE(d.is_view());
    REQUIRE(d.root()["a"] == (int64_t)1);

    // Truncated string at the content end: positioned ParseError in the
    // channel, offset == content_len (NUL padding stops the SIMD scan)
    std::string bad = R"("abc)";
    std::string bad_buf(bad.size() + kPaddingWidth, '\0');
    memcpy(bad_buf.data(), bad.data(), bad.size());
    auto er = parse_view_result(bad_buf.data(), bad.size());
    REQUIRE(er.is_err());
    ParseError e = er.unwrap_err();
    REQUIRE(e.offset() == 4); // "Unterminated string" @ content end
    REQUIRE(e.category() == Category::Parse);
    REQUIRE(dynamic_cast<const ParseError *>(&e) != nullptr);
}

TEST_CASE("Parser: result moved state") {
    auto r = parse_copy_result(R"({"a":1})");
    REQUIRE(r.is_ok());
    Document d = std::move(r).unwrap(); // moves the Document out; r enters Moved
    REQUIRE(r.is_moved());
    REQUIRE(d.root()["a"].is_int());
    // wrong-state extractors on a Moved Result throw (stricter than the old
    // precondition contract; bad_result_access per pjh_result)
    // void-cast: the extractors are [[nodiscard]], the throw is the pin
    REQUIRE_THROWS_AS(static_cast<void>(r.unwrap()), pjh::result::bad_result_access);
    REQUIRE_THROWS_AS(static_cast<void>(r.expect("moved")), pjh::result::bad_result_access);
}

TEST_CASE("Error: category") {
    REQUIRE(JsonError("x").category() == Category::Json);
    REQUIRE(ParseError("x", 3).category() == Category::Parse);
    REQUIRE(TypeError("x").category() == Category::Type);
    // Inheritance channel: the base-class reference observes the derived
    // value (no slicing, runtime polymorphism)
    const JsonError &base = ParseError("x", 7);
    REQUIRE(base.category() == Category::Parse);
}

TEST_CASE("Parser: BOM rejected by default") {
    // Default-OFF contract pin (task 23): a leading UTF-8 BOM (EF BB BF)
    // is a grammar error by default. Full message: "Unexpected character
    // parsing value at offset 0" (value.cpp default branch) — what()'s
    // full text is not the house contract pin (error.hpp:47-50), the
    // offset segment is.
    try {
        (void)parse_copy(bom() + "1");
        REQUIRE(false);
    } catch (const ParseError &e) {
        REQUIRE(e.offset() == 0);
        REQUIRE(std::string(e.what()).find(" at offset 0") != std::string::npos);
    }

    // Control: BOM-free input succeeds (the BOM is the sole cause)
    auto ok = parse_copy("1");
    REQUIRE(ok.root().as_int() == (int64_t)1);

    // A BOM after whitespace is not at byte 0: no strip, the 0xEF at
    // input index 1 is an ordinary invalid byte
    try {
        (void)parse_copy(" " + bom() + "1");
        REQUIRE(false);
    } catch (const ParseError &e) {
        REQUIRE(e.offset() == 1);
    }
}

TEST_CASE("Parser: BOM strip opt-in") {
    ConfigGuard guard; // first: save the entering state before any mutation
    Config::instance().set_strip_bom(true);
    using std::pmr::get_default_resource;

    // copy: the BOM is stripped, the parse succeeds; the BOM bytes stay in
    // the Document buffer (no buffer rewrite — offset-honesty bookkeeping)
    auto d1 = parse_copy(bom() + R"({"a":1})");
    REQUIRE(d1.root()["a"] == (int64_t)1);
    REQUIRE(d1.buffer().compare(0, 3, "\xEF\xBB\xBF", 3) == 0);

    // in_situ: head BOM + compliant tail padding (the tail-NUL check and
    // the head BOM have zero interaction)
    std::pmr::string isitu(get_default_resource());
    isitu.assign(bom() + R"([1,2])");
    isitu.append(kPaddingWidth, '\0');
    auto d2 = parse_in_situ(std::move(isitu));
    REQUIRE(d2.root().size() == 2);

    // view: caller-padded buffer with the BOM inside the content
    std::string content = bom() + R"({"a":1})";
    std::string buf(content.size() + kPaddingWidth, '\0');
    memcpy(buf.data(), content.data(), content.size());
    auto d3 = parse_view(buf.data(), content.size());
    REQUIRE(d3.root()["a"] == (int64_t)1);

    // Offset honesty (ruling D): the strip advances m_curr, never
    // m_begin — the extra '2' sits at original-buffer index 5 (EF BB BF
    // '1' ' ' '2'); a shifted-view implementation would report 2
    try {
        (void)parse_copy(bom() + "1 2");
        REQUIRE(false);
    } catch (const ParseError &e) {
        REQUIRE(e.offset() == 5);
    }

    // BOM-only input: after the strip the content is empty — "Unexpected
    // end of input" at offset 3, m_begin untouched
    try {
        (void)parse_copy(bom());
        REQUIRE(false);
    } catch (const ParseError &e) {
        REQUIRE(e.offset() == 3);
    }

    // Truncated BOM (EF BB + '1'): third byte mismatches, no strip, the
    // 0xEF is rejected at offset 0
    try {
        (void)parse_copy(std::string("\xEF\xBB", 2) + "1");
        REQUIRE(false);
    } catch (const ParseError &e) {
        REQUIRE(e.offset() == 0);
    }

    // UTF-16 BOMs get no special-casing (ruling E): with strip ON they
    // are still invalid UTF-8, rejected at offset 0
    try {
        (void)parse_copy(std::string("\xFF\xFE", 2) + "1");
        REQUIRE(false);
    } catch (const ParseError &e) {
        REQUIRE(e.offset() == 0);
    }
    try {
        (void)parse_copy(std::string("\xFE\xFF", 2) + "1");
        REQUIRE(false);
    } catch (const ParseError &e) {
        REQUIRE(e.offset() == 0);
    }

    // The result channel inherits the knob with zero shell code
    auto r1 = parse_copy_result(bom() + "1");
    REQUIRE(r1.is_ok());

    Config::instance().set_strip_bom(false);
    auto r2 = parse_copy_result(bom() + "1");
    REQUIRE(r2.is_err());
    ParseError e2 = r2.unwrap_err();
    REQUIRE(e2.offset() == 0);
}

TEST_CASE("Parser: jsonl BOM line-1 only") {
    ConfigGuard guard; // first: save the entering state before any mutation
    Config::instance().set_strip_bom(true);

    // Whole-input start: the BOM belongs to the file and is consumed once
    // before the line scan; line 1's parser sees BOM-free content
    auto d = parse_jsonl(bom() + "1\n2\n");
    REQUIRE(d.root().is_array());
    REQUIRE(d.root().size() == 2);
    REQUIRE(d.root()[0] == (int64_t)1);
    REQUIRE(d.root()[1] == (int64_t)2);

    // Line >= 2: per-line parsers keep the strict grammar — a BOM at the
    // line start is a parse error at that line's offset 0 (the offset is
    // relative to the line, house jsonl contract)
    try {
        (void)parse_jsonl("1\n" + bom() + "2\n");
        REQUIRE(false);
    } catch (const ParseError &e) {
        REQUIRE(e.offset() == 0);
    }

    // strip OFF: the default contract in its jsonl form
    Config::instance().set_strip_bom(false);
    try {
        (void)parse_jsonl(bom() + "1\n");
        REQUIRE(false);
    } catch (const ParseError &e) {
        REQUIRE(e.offset() == 0);
    }
}

TEST_CASE("Parser: invalid UTF-8 accepted by default")
{
    // Default-OFF contract pin (task 24): string content is a byte
    // mirror — ill-formed UTF-8 is accepted and round-trips
    // byte-identical. This case has NO guard: it must stay green
    // forever (the regression wall for the default contract).
    std::string samples[] = {
        invalid_byte(),    // 0xFF: no such lead byte
        lone_cont(),       // 0x80: continuation without a lead
        overlong2(),       // C0 80: overlong 2-byte
        overlong3(),       // E0 80 80: overlong 3-byte
        surrogate_seq(),   // ED A0 80: U+D800 in UTF-8
        over_10ffff(),     // F5 80 80 80: no such lead byte
        truncated_tail(),  // E0 at the string end
    };
    for (const std::string &s : samples)
    {
        auto d = parse_copy("\"" + s + "\"");
        REQUIRE(d.root().as_string() == std::string_view(s));
    }

    // Control: clean input succeeds (rejection is attributed correctly)
    auto ok = parse_copy("\"hi\"");
    REQUIRE(ok.root().as_string() == std::string_view("hi"));

    // Unterminated-string priority: no closing quote -> the SIMD scan
    // hits the padding NUL first; "Unterminated string" at the content
    // end, NOT a UTF-8 error (the checker never runs without a quote
    // window). Content = " a b c E0 (5 bytes).
    try {
        (void)parse_copy(std::string("\"abc\xE0", 5));
        REQUIRE(false);
    } catch (const ParseError &e) {
        REQUIRE(e.offset() == 5);
        REQUIRE(std::string(e.what()).find("Unterminated") != std::string::npos);
    }
}

TEST_CASE("Parser: strict UTF-8 rejects malformed bytes")
{
    ConfigGuard guard; // first: save the entering state before any mutation

    // Phase A — default (strict OFF): every sample passes, byte-identical
    std::string samples[] = { invalid_byte(), lone_cont(), overlong2(),
        overlong3(), surrogate_seq(), over_10ffff(), truncated_tail() };
    for (const std::string &s : samples)
        REQUIRE(parse_copy("\"" + s + "\"").root().as_string() == std::string_view(s));

    Config::instance().set_strict_utf8(true);

    // Phase B — strict ON: each violation class rejected at its first
    // offending byte. Pin = type + offset + " at offset N" substring
    // (task 13 caliber: what()'s full text is not the contract); the
    // message-family substring is added where the class is unambiguous.
    auto expect_off = [](std::string input, size_t off,
                         std::string_view family)
    {
        try {
            (void)parse_copy(input);
            REQUIRE(false);
        } catch (const ParseError &e) {
            REQUIRE(e.offset() == off);
            REQUIRE(std::string(e.what()).find(" at offset " + std::to_string(off))
                    != std::string::npos);
            if (!family.empty())
                REQUIRE(std::string(e.what()).find(family) != std::string::npos);
        }
    };

    expect_off("\"a\xFF\"", 2, "Invalid UTF-8 lead byte in string");   // bad lead
    expect_off("\"a\x80\"", 2, "Invalid UTF-8 lead byte in string");   // lone continuation
    expect_off("\"a\xC0\x80\"", 2, "Overlong UTF-8 sequence in string");
    expect_off("\"a\xE0\x80\x80\"", 2, "Overlong UTF-8 sequence in string");
    expect_off("\"a\xED\xA0\x80\"", 2, "UTF-8 surrogate codepoint in string");
    expect_off("\"a\xED\xBF\xBF\"", 2, "UTF-8 surrogate codepoint in string"); // U+DFFF edge
    expect_off("\"a\xF5\x80\x80\x80\"", 2, "");  // 0xF5: no such lead byte
    expect_off("\"a\xE0\"", 2, "Truncated UTF-8 sequence in string");
    expect_off("\"a\xE0\xC0\"", 3, "Invalid UTF-8 continuation byte in string"); // lead in a slot

    // 24.1: the two constrained 3-byte leads 0xE0/0xED are lead + TWO
    // continuations. The old need=1 cleared the state after the first
    // slot, so a sequence cut one byte short was accepted as complete;
    // the corrected need=2 leaves it dangling and end() reports
    // truncation at the sequence lead (offset 2, same as "a\xE0" above).
    expect_off("\"a\xE0\xA0\"", 2, "Truncated UTF-8 sequence in string");
    expect_off("\"a\xED\x80\"", 2, "Truncated UTF-8 sequence in string");

    // Legal multibyte control group (strict ON must pass): the
    // signed-char trap's executable counter-evidence
    auto vd = parse_copy("\"" + valid_utf8() + "\"");
    REQUIRE(vd.root().as_string() == std::string_view(valid_utf8()));

    // 24.1: completed 3-byte sequences for the constrained leads must
    // pass with the corrected count — valid U+0800–U+0FFF (E0) and
    // U+D000–U+D7FF (ED), both edges. E1/EE are mid-range controls for
    // the already-correct unconstrained 3-byte branch.
    auto cp_0800 = parse_copy("\"\xE0\xA0\x80\""); // U+0800 (E0 lower edge)
    REQUIRE(cp_0800.root().as_string() == std::string_view("\xE0\xA0\x80", 3));
    auto cp_0fff = parse_copy("\"\xE0\xBF\xBF\""); // U+0FFF (E0 upper edge)
    REQUIRE(cp_0fff.root().as_string() == std::string_view("\xE0\xBF\xBF", 3));
    auto cp_d000 = parse_copy("\"\xED\x80\x80\""); // U+D000 (ED lower edge)
    REQUIRE(cp_d000.root().as_string() == std::string_view("\xED\x80\x80", 3));
    auto cp_d7ff = parse_copy("\"\xED\x9F\xBF\""); // U+D7FF (ED upper edge)
    REQUIRE(cp_d7ff.root().as_string() == std::string_view("\xED\x9F\xBF", 3));
    auto cp_1000 = parse_copy("\"\xE1\x80\x80\""); // U+1000 (E1 mid-range control)
    REQUIRE(cp_1000.root().as_string() == std::string_view("\xE1\x80\x80", 3));
    auto cp_e000 = parse_copy("\"\xEE\x80\x80\""); // U+E000 (EE mid-range control)
    REQUIRE(cp_e000.root().as_string() == std::string_view("\xEE\x80\x80", 3));

    // Same rule core on the Phase-2 stream path (the escape forces the
    // in-situ decode): valid E0 3-byte + \n must pass.
    auto p2 = parse_copy("\"\xE0\xA0\x80\\n\"");
    REQUIRE(p2.root().as_string() == std::string_view("\xE0\xA0\x80\n", 4));

    // Escape face: already strict before this task; the knob neither
    // adds nor removes anything there (only type + offset pinned)
    auto pair = parse_copy(R"("\uD83D\uDE00")");
    REQUIRE(pair.root().as_string() == std::string_view("\xF0\x9F\x98\x80"));
    try {
        (void)parse_copy(R"("\uD800")");
        REQUIRE(false);
    } catch (const ParseError &e) {
        REQUIRE(e.offset() == 7); // lone high: cursor past \uD800's hex4
    }
    try {
        (void)parse_copy(R"("\uDC00")");
        REQUIRE(false);
    } catch (const ParseError &e) {
        REQUIRE(e.offset() == 7); // lone low
    }

    // Escape tear: 0xC3 (2-byte lead) followed by an escape — the escape
    // source text is fed as a plain byte stream: the backslash is the
    // bad first continuation, reported at the backslash (NOT at the
    // decoded output, NOT an interval pass over the post-decode buffer)
    expect_off(std::string("\"a\xC3", 3) + "\\u0041\"", 3,
               "Invalid UTF-8 continuation byte in string");

    // Object keys go through parse_string (object.cpp) — the gate covers
    // keys, not just values
    expect_off(std::string("{\"\xFF\":1}", 7), 2,
               "Invalid UTF-8 lead byte in string");

    // Result channel: the thin shell inherits the knob (task 16 precedent)
    auto r = parse_copy_result("\"a\xFF\"");
    REQUIRE(r.is_err());
    ParseError e = r.unwrap_err();
    REQUIRE(e.offset() == 2);

    // DEL (0x7F) stays legal under strict ON (literal_test.cpp:209 mirror)
    auto del = parse_copy("\"a\x7F\"");
    REQUIRE(del.root().as_string() == std::string_view("a\x7F", 2));
}

TEST_CASE("Parser: strict UTF-8 jsonl line offset")
{
    ConfigGuard guard; // first: save the entering state before any mutation
    Config::instance().set_strict_utf8(true);

    // Line 2 = "a\xFF": offset is RELATIVE TO THE LINE, not the whole
    // input (a whole-buffer reading would say 4). This pin is the
    // executable referee for the house jsonl line-offset contract
    // (document.hpp parse_jsonl note; "jsonl result entry" precedent).
    // First error wins: line 1 legal, line 2 illegal -> the throw IS the
    // pin (the partial document is discarded, nothing more observable).
    try {
        (void)parse_jsonl("1\n\"a\xFF\"\n");
        REQUIRE(false);
    } catch (const ParseError &e) {
        REQUIRE(e.offset() == 2); // " = 0, a = 1, 0xFF = 2 (line-relative)
        REQUIRE(std::string(e.what()).find(" at offset 2") != std::string::npos);
        REQUIRE(std::string(e.what()).find("Invalid UTF-8 lead byte") != std::string::npos);
    }

    // strict OFF: the default contract in its jsonl form
    Config::instance().set_strict_utf8(false);
    auto d = parse_jsonl("1\n\"a\xFF\"\n");
    REQUIRE(d.root().is_array());
    REQUIRE(d.root().size() == 2);
}

TEST_CASE("Parser: strict UTF-8 BOM boundary")
{
    ConfigGuard guard; // first: save the entering state before any mutation
    Config::instance().set_strip_bom(false);
    Config::instance().set_strict_utf8(true);

    // BOM at byte 0 under strict ON: the BOM is NOT a UTF-8 error — it
    // sits at the top level, where the grammar rejects it ("Unexpected
    // character parsing value" @ 0, value.cpp). The checker only sees
    // bytes inside quotes; the two knobs rule on disjoint bytes
    try {
        (void)parse_copy(bom() + "1");
        REQUIRE(false);
    } catch (const ParseError &e) {
        REQUIRE(e.offset() == 0);
        REQUIRE(std::string(e.what()).find(" at offset 0") != std::string::npos);
        REQUIRE(std::string(e.what()).find("Unexpected character") != std::string::npos);
    }

    // BOM bytes INSIDE a string: U+FEFF data, legal UTF-8 — strict ON
    // passes it byte-identical (strip_bom's byte-0 rule does not reach
    // inside quotes)
    auto d = parse_copy("\"" + bom() + "\"");
    REQUIRE(d.root().as_string() == std::string_view(bom()));

    // Both-knob combination (task 23 + 24 orthogonality, executable):
    // strip_bom ON strips the byte-0 prefix, strict ON validates the
    // (BOM-free) content
    Config::instance().set_strip_bom(true);
    auto ok = parse_copy(bom() + "1");
    REQUIRE(ok.root().as_int() == (int64_t)1);
}

TEST_CASE("Parser: kernel status") {
    // success: the zero-throw kernel reports true and an empty slot
    std::string good = R"({"a":1})";
    good.append(kPaddingWidth, '\0');
    Parser p(std::string_view(good.data(), good.size() - kPaddingWidth),
             Config::instance().resource(), true);
    Json root;
    REQUIRE(p.parse(root));
    REQUIRE_FALSE(p.error().has_error());
    REQUIRE(root["a"] == (int64_t)1);

    // failure: the kernel reports false and the slot holds the first error
    std::string bad = R"({"a":})";
    bad.append(kPaddingWidth, '\0');
    Parser q(std::string_view(bad.data(), bad.size() - kPaddingWidth),
             Config::instance().resource(), true);
    Json out;
    REQUIRE_FALSE(q.parse(out));
    REQUIRE(q.error().has_error());
    REQUIRE(q.error().position == 5); // '}' after ':'
    REQUIRE(q.error().category == Category::Parse);
    REQUIRE(q.error().positioned);

    // first error wins: a second call cannot overwrite the slot
    const size_t pos = q.error().position;
    Json out2;
    REQUIRE_FALSE(q.parse(out2));
    REQUIRE(q.error().position == pos);

    // the compatibility shell still throws the byte-identical positioned
    // ParseError materialised from the same slot
    try {
        (void)q.parse();
        REQUIRE(false);
    } catch (const ParseError &e) {
        REQUIRE(e.offset() == pos);
        REQUIRE(e.category() == Category::Parse);
        REQUIRE(std::string(e.what()) ==
                "Unexpected character at offset 5");
    }
}

TEST_CASE("Parser: shell equivalence") {
    // The throwing entry and the Result entry are two shells over the same
    // *_impl: for every input the error state, offset, category and what()
    // must agree byte-for-byte.
    auto check = [](std::string_view in) {
        bool threw = false;
        size_t off = 0;
        Category cat = Category::Json;
        std::string what;
        try {
            (void)parse_copy(in);
        } catch (const ParseError &e) {
            threw = true;
            off = e.offset();
            cat = e.category();
            what = e.what();
        }
        auto r = parse_copy_result(in);
        REQUIRE(r.is_err() == threw);
        if (threw) {
            ParseError got = r.unwrap_err();
            REQUIRE(got.offset() == off);
            REQUIRE(got.category() == cat);
            REQUIRE(std::string(got.what()) == what);
        } else {
            REQUIRE(r.is_ok());
        }
    };
    check(R"({"a":1,"b":[true,null]})");
    check("{");
    check("[1,2");
    check("1e400");
    check("\"abc");
    check("trueX");
    check("");
}

TEST_CASE("Parser: result duplicate-key detail lifetime") {
    ConfigGuard guard; // save/restore strict_duplicate_keys
    Config::instance().set_strict_duplicate_keys(true);

    // The borrowed key detail must be copied into the owned what() BEFORE the
    // impl-local parse buffer dies (ASan pins the UAF if materialised late).
    const char *in = R"({"a":1,"a":2})";
    auto r = parse_copy_result(in);
    REQUIRE(r.is_err());
    ParseError e = r.unwrap_err();
    REQUIRE(std::string(e.what()) == "Duplicate key \"a\" in object");
    REQUIRE(e.offset() == 0);
    REQUIRE(e.category() == Category::Parse);

    bool threw = false;
    try { (void)parse_copy(in); }
    catch (const ParseError &t) {
        threw = true;
        REQUIRE(std::string(t.what()) == std::string(e.what()));
        REQUIRE(t.offset() == e.offset());
    }
    REQUIRE(threw);
}

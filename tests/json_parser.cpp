#include <doctest/doctest.h>
#include <string_view>
#include <cstring>
#include <stdexcept>
#include <cmath>
#include <limits>

#include <xsimd/xsimd.hpp>

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

        ConfigGuard()
            : m_strict(Config::instance().strict_duplicate_keys()),
              m_arena_block(Config::instance().arena_block_size()),
              m_max_depth(Config::instance().max_depth())
        {
        }

        ~ConfigGuard()
        {
            Config::instance().set_strict_duplicate_keys(m_strict);
            Config::instance().set_arena_block_size(m_arena_block);
            Config::instance().set_max_depth(m_max_depth);
        }
    };
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
    Config::instance().set_max_depth(0); // defensive: clear prior case state

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

    // Default: unlimited (regression pin)
    REQUIRE(Config::instance().max_depth() == 0);
    auto doc100 = parse_copy(deep(100, '[', ']'));
    REQUIRE(depth_of(doc100.root()) == 100);

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

    Config::instance().set_max_depth(0); // restore
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
    // contract width (2x the SIMD batch) must parse. A narrower contract
    // leaves NULs inside the content range and trips "Extra characters
    // after complete JSON value".
    std::pmr::string isitu_buf(get_default_resource());
    isitu_buf.assign(R"({"a":1})");
    isitu_buf.append(2 * xsimd::batch<uint8_t>::size, '\0');
    auto isitu_doc = parse_in_situ(std::move(isitu_buf));
    REQUIRE(isitu_doc.root()["a"] == (int64_t)1);
}

TEST_CASE("Parser: padding width contract") {
    // The documented contract (parser.hpp / document.hpp) pins the padding
    // to kPaddingWidth NUL bytes; the width is 2x the SIMD batch, derived at
    // compile time so an ISA widening keeps the 2x invariant automatically.
    // Pin the identity so a padding change must update this test and the
    // docs in lockstep.
    REQUIRE(kPaddingWidth == 2 * xsimd::batch<uint8_t>::size);
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
    REQUIRE(r.has_value());
    REQUIRE(r.value().root()["a"].is_int());
    REQUIRE(r.value().root()["b"].is_array());
    // value() hands out a reference; the expected itself stays usable
    REQUIRE(r.value().root()["b"].size() == 2);
    REQUIRE(static_cast<bool>(r));
}

TEST_CASE("Parser: result entry positioned error") {
    auto r = parse_copy_result("{"); // "Expected string key in object"
    REQUIRE(!r.has_value());
    REQUIRE(r.error().offset() == 1); // '{' consumed, cursor at padding NUL
    REQUIRE(r.error().category() == Category::Parse);
    // no-slicing pin: the channel still holds the full ParseError (dynamic
    // type + machine channel survive the by-value copy; what() carries the
    // offset segment, task 13 shape, mode-independent)
    REQUIRE(dynamic_cast<const ParseError *>(&r.error()) != nullptr);
    REQUIRE(std::string(r.error().what()).find(" at offset 1") != std::string::npos);
}

TEST_CASE("Parser: result entry context-free error") {
    ConfigGuard guard; // first: save the entering state before any mutation
    Config::instance().set_strict_duplicate_keys(true);

    // in_situ size below kPaddingWidth (offset 0, context-free)
    std::pmr::string small;
    small.resize(10, '\0');
    auto r1 = parse_in_situ_result(std::move(small));
    REQUIRE(!r1.has_value());
    REQUIRE(r1.error().offset() == 0);
    REQUIRE(r1.error().category() == Category::Parse);

    // strict duplicate key (offset 0, context-free)
    auto r2 = parse_copy_result(R"({"a":1,"a":2})");
    REQUIRE(!r2.has_value());
    REQUIRE(r2.error().offset() == 0);
    REQUIRE(r2.error().category() == Category::Parse);
}

TEST_CASE("Parser: jsonl result entry") {
    // Line 2 fails: offset is RELATIVE TO THE LINE, not the whole input
    // ('x' sits at whole-input offset 7 but line offset 3)
    auto r = parse_jsonl_result("[1]\n[2 x]\n[3]");
    REQUIRE(!r.has_value());
    REQUIRE(r.error().category() == Category::Parse);
    REQUIRE(r.error().offset() == 3);
    REQUIRE(dynamic_cast<const ParseError *>(&r.error()) != nullptr);

    // All lines legal -> one Array with one element per line
    auto ok = parse_jsonl_result("[1]\n[2]\n[3]");
    REQUIRE(ok.has_value());
    REQUIRE(ok.value().root().as_array().size() == 3);
}

TEST_CASE("Parser: view result entry") {
    // The result form never runtime-verifies caller padding (unlike
    // parse_in_situ_result); the buffer is the contract-legal padded form,
    // with the JSON itself truncated at the content end
    std::string content = R"({"a":1})";
    std::string buf(content.size() + kPaddingWidth, '\0');
    memcpy(buf.data(), content.data(), content.size());
    auto r = parse_view_result(buf.data(), content.size());
    REQUIRE(r.has_value());
    REQUIRE(r.value().is_view());
    REQUIRE(r.value().root()["a"] == (int64_t)1);

    // Truncated string at the content end: positioned ParseError in the
    // channel, offset == content_len (NUL padding stops the SIMD scan)
    std::string bad = R"("abc)";
    std::string bad_buf(bad.size() + kPaddingWidth, '\0');
    memcpy(bad_buf.data(), bad.data(), bad.size());
    auto er = parse_view_result(bad_buf.data(), bad.size());
    REQUIRE(!er.has_value());
    REQUIRE(er.error().offset() == 4); // "Unterminated string" @ content end
    REQUIRE(er.error().category() == Category::Parse);
    REQUIRE(dynamic_cast<const ParseError *>(&er.error()) != nullptr);
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

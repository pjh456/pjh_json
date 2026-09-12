#include <doctest/doctest.h>

#include <cstdint>
#include <limits>
#include <ostream> // doctest stringification on MSVC needs a complete ostream
#include <string>
#include <string_view>
#include <utility>

#include <pjh_json/config.hpp>
#include <pjh_json/document.hpp>
#include <pjh_json/error.hpp>
#include <pjh_json/json.hpp>
#include <pjh_json/writer.hpp>

using namespace pjh::json;

namespace
{
    // doctest has no per-case teardown: restore EVERY knob this TU can touch
    // on an unwinding REQUIRE, so the JSON5 mode cannot leak into the
    // default-RFC tests (plan 40 §7 R4).
    struct Json5ConfigGuard
    {
        bool m_strict;
        size_t m_arena_block;
        size_t m_max_depth;
        bool m_strip_bom;
        bool m_strict_utf8;
        bool m_json5;

        Json5ConfigGuard()
            : m_strict(Config::instance().strict_duplicate_keys()),
              m_arena_block(Config::instance().arena_block_size()),
              m_max_depth(Config::instance().max_depth()),
              m_strip_bom(Config::instance().strip_bom()),
              m_strict_utf8(Config::instance().strict_utf8()),
              m_json5(Config::instance().json5())
        {
        }

        ~Json5ConfigGuard()
        {
            Config::instance().set_strict_duplicate_keys(m_strict);
            Config::instance().set_arena_block_size(m_arena_block);
            Config::instance().set_max_depth(m_max_depth);
            Config::instance().set_strip_bom(m_strip_bom);
            Config::instance().set_strict_utf8(m_strict_utf8);
            Config::instance().set_json5(m_json5);
        }
    };

    // One VT (0x0B) / FF (0x0C) byte at a time: a greedy \x escape in a
    // literal would otherwise swallow a following hex-ish character.
    std::string vt()
    {
        return std::string(1, '\x0B');
    }
    std::string ff()
    {
        return std::string(1, '\x0C');
    }

    std::string_view sv(const std::pmr::string &s)
    {
        return std::string_view(s.data(), s.size());
    }
}

TEST_CASE("Json5: knob default-off and toggles")
{
    Json5ConfigGuard guard;
    Config &cfg = Config::instance();

    // Default mode is strict RFC; a leaked JSON5 mode would trip this.
    CHECK(cfg.json5() == false);

    cfg.set_json5(true);
    REQUIRE(cfg.json5() == true);

    cfg.set_json5(false);
    REQUIRE(cfg.json5() == false);
}

TEST_CASE("Json5: default mode rejects JSON5")
{
    Json5ConfigGuard guard;
    Config::instance().set_json5(false);

    CHECK_THROWS_AS((void)parse_copy("{a:1}"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("'x'"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("[1,]"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("{a:1,}"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("//c\n1"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("/*c*/1"), ParseError);
    CHECK_THROWS_AS((void)parse_copy(vt() + "1" + ff()), ParseError);
}

TEST_CASE("Json5: comments")
{
    Json5ConfigGuard guard;
    Config::instance().set_json5(true);

    REQUIRE(parse_copy("// c\n1").root().as_int() == (int64_t)1);
    REQUIRE(parse_copy("1 // done").root().as_int() == (int64_t)1);
    REQUIRE(parse_copy("1 //").root().as_int() == (int64_t)1);
    REQUIRE(parse_copy("/* block */ 1").root().as_int() == (int64_t)1);
    REQUIRE(parse_copy("1 /* trailing */").root().as_int() == (int64_t)1);
    REQUIRE(parse_copy("/* a\n b */ [1]").root().is_array());

    auto arr = parse_copy("[1, // c\n 2 /* x */, 3]");
    REQUIRE(arr.root().size() == 3);
    REQUIRE(arr.root()[1].as_int() == (int64_t)2);

    // Non-nesting: the first star-slash closes the comment.
    REQUIRE(parse_copy("/* /* */ 1").root().as_int() == (int64_t)1);

    // Comment content must not disturb the grammar.
    REQUIRE(parse_copy("[1 /* \"x\" { // */ , 2]").root().size() == 2);

    // Trivia is legal between a key, its colon and its value.
    REQUIRE(parse_copy("{a /* key */ : /* value */ 1}").root()["a"].as_int() == (int64_t)1);

    // Unterminated block comment: reuse UnexpectedEndOfInput (no new code).
    CHECK_THROWS_AS((void)parse_copy("/*"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("/*/"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("[1 /*"), ParseError);
}

TEST_CASE("Json5: trailing commas")
{
    Json5ConfigGuard guard;
    Config::instance().set_json5(true);

    REQUIRE(parse_copy("[1,]").root().size() == 1);
    REQUIRE(parse_copy("[1,2,]").root().size() == 2);
    REQUIRE(parse_copy("[1, ]").root().size() == 1);
    REQUIRE(parse_copy("[1,/*c*/]").root().size() == 1);
    REQUIRE(parse_copy(R"({"a":1,})").root()["a"].as_int() == (int64_t)1);
    REQUIRE(parse_copy("{a:1,}").root()["a"].as_int() == (int64_t)1);
    REQUIRE(parse_copy("[{\"a\":1,},]").root().size() == 1);
    REQUIRE(parse_copy("{\"a\":[1,],}").root()["a"].size() == 1);

    CHECK_THROWS_AS((void)parse_copy("[,]"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("{,}"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("[1,,]"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("{a:1,,}"), ParseError);
}

TEST_CASE("Json5: single-quoted strings")
{
    Json5ConfigGuard guard;
    Config::instance().set_json5(true);

    REQUIRE(parse_copy("'x'").root().as_string() == std::string_view("x"));
    REQUIRE(parse_copy("''").root().as_string().empty());
    REQUIRE(parse_copy("'a\"b'").root().as_string() == std::string_view("a\"b"));
    REQUIRE(parse_copy("\"a'b\"").root().as_string() == std::string_view("a'b"));

    // Escape set matches the RFC double-quote path byte for byte.
    REQUIRE(parse_copy("'a\\nb'").root().as_string() == std::string_view("a\nb"));
    REQUIRE(parse_copy("'\\u0041'").root().as_string() == std::string_view("A"));
    REQUIRE(parse_copy("'\\uD83D\\uDE00'").root().as_string() == std::string_view("\xF0\x9F\x98\x80", 4));
    REQUIRE(parse_copy("'a\\nb'").root() == parse_copy("\"a\\nb\"").root());

    // Single-quoted key.
    REQUIRE(parse_copy("{'q':1}").root()["q"].as_int() == (int64_t)1);

    // Unterminated / raw control byte (line continuation is deferred to 40.3).
    CHECK_THROWS_AS((void)parse_copy("'"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("'abc"), ParseError);
    CHECK_THROWS_AS((void)parse_copy(std::string_view("'a\nb'", 5)), ParseError);

    // strict_utf8 stays orthogonal and applies inside '...' too.
    std::string bad = "'";
    bad.push_back(static_cast<char>(0xFF));
    bad += "'";
    Config::instance().set_strict_utf8(false);
    REQUIRE(parse_copy(bad).root().is_string());
    Config::instance().set_strict_utf8(true);
    CHECK_THROWS_AS((void)parse_copy(bad), ParseError);
}

TEST_CASE("Json5: unquoted identifier keys")
{
    Json5ConfigGuard guard;
    Config::instance().set_json5(true);

    auto doc = parse_copy("{a:1,_b:2,$c:3,while:4,\"a b\":5}");
    REQUIRE(doc.root().size() == 5);
    REQUIRE(doc.root()["a"].as_int() == (int64_t)1);
    REQUIRE(doc.root()["_b"].as_int() == (int64_t)2);
    REQUIRE(doc.root()["$c"].as_int() == (int64_t)3);
    REQUIRE(doc.root()["while"].as_int() == (int64_t)4);
    REQUIRE(doc.root()["a b"].as_int() == (int64_t)5);

    // Digits are continuation characters, not start characters.
    REQUIRE(parse_copy("{a1b2:7}").root()["a1b2"].as_int() == (int64_t)7);

    CHECK_THROWS_AS((void)parse_copy("{1:2}"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("{-:1}"), ParseError);

    // strict_duplicate_keys compares identifier and quoted keys alike.
    Config::instance().set_strict_duplicate_keys(true);
    CHECK_THROWS_AS((void)parse_copy("{a:1,\"a\":2}"), ParseError);
    Config::instance().set_strict_duplicate_keys(false);
    REQUIRE(parse_copy("{a:1,a:2}").root()["a"].as_int() == (int64_t)2);
    REQUIRE(parse_copy("{a:1,\"a\":2}").root()["a"].as_int() == (int64_t)2);
}

TEST_CASE("Json5: VT/FF ASCII whitespace")
{
    Json5ConfigGuard guard;
    Config &cfg = Config::instance();

    cfg.set_json5(false);
    CHECK_THROWS_AS((void)parse_copy(vt() + "1" + ff()), ParseError);
    CHECK_THROWS_AS((void)parse_copy("{" + vt() + "\"a\"" + ff() + ":" + vt() + "1" + ff() + "}"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("true" + vt()), ParseError);

    cfg.set_json5(true);
    REQUIRE(parse_copy(vt() + "1" + ff()).root().as_int() == (int64_t)1);
    auto obj = parse_copy("{" + vt() + "\"a\"" + ff() + ":" + vt() + "1" + ff() + "}");
    REQUIRE(obj.root()["a"].as_int() == (int64_t)1);
    REQUIRE(parse_copy("true" + vt()).root().as_boolean() == true);
}

TEST_CASE("Json5: jsonl blank-line and line-bounded trivia (R1)")
{
    Json5ConfigGuard guard;
    Config &cfg = Config::instance();

    const std::string input = vt() + "\n[1]\n";

    cfg.set_json5(false);
    CHECK_THROWS_AS((void)parse_jsonl(input), ParseError);

    cfg.set_json5(true);
    auto doc = parse_jsonl(input);
    REQUIRE(doc.root().is_array());
    REQUIRE(doc.root().size() == 1); // must NOT become [[1],[1]]
    REQUIRE(doc.root()[0].is_array());
    REQUIRE(doc.root()[0].size() == 1);
    REQUIRE(doc.root()[0][0].as_int() == (int64_t)1);

    // A line ending in an unterminated block comment must not consume the
    // next line: the line errors instead.
    CHECK_THROWS_AS((void)parse_jsonl("/*\n[1]\n"), ParseError);

    // A VT+FF-only line is blank under JSON5.
    auto doc2 = parse_jsonl(vt() + ff() + "\n[1]\n");
    REQUIRE(doc2.root().size() == 1);
}

TEST_CASE("Json5: JSON5 input dumps as RFC 8259")
{
    Json5ConfigGuard guard;
    Config::instance().set_json5(true);

    auto doc = parse_copy("{a:'x', b:[1,2,], /*c*/ c:'y',}");
    REQUIRE(sv(dump(doc)) == R"({"a":"x","b":[1,2],"c":"y"})");
}

TEST_CASE("Json5: hexadecimal numbers")
{
    Json5ConfigGuard guard;
    Config::instance().set_json5(true);

    auto d1 = parse_copy("0xdecaf");
    REQUIRE(d1.root().is_int());
    REQUIRE(d1.root().as_int() == (int64_t)912559);

    auto d2 = parse_copy("0X10");
    REQUIRE(d2.root().is_int());
    REQUIRE(d2.root().as_int() == (int64_t)16);

    auto d3 = parse_copy("-0xC0FFEE");
    REQUIRE(d3.root().is_int());
    REQUIRE(d3.root().as_int() == (int64_t)-12648430);

    auto d4 = parse_copy("+0x1F");
    REQUIRE(d4.root().is_int());
    REQUIRE(d4.root().as_int() == (int64_t)31);

    // Leading zeros are insignificant: `0x0001` is an int with one
    // significant digit, not a 64-bit overflow.
    auto d5 = parse_copy("0x0000000000000000001");
    REQUIRE(d5.root().is_int());
    REQUIRE(d5.root().as_int() == (int64_t)1);

    // int64 boundaries stay exact.
    auto m1 = parse_copy("0x7FFFFFFFFFFFFFFF");
    REQUIRE(m1.root().is_int());
    REQUIRE(m1.root().as_int() == std::numeric_limits<int64_t>::max());
    auto m2 = parse_copy("-0x8000000000000000");
    REQUIRE(m2.root().is_int());
    REQUIRE(m2.root().as_int() == std::numeric_limits<int64_t>::min());

    // Beyond int64 -> double, same policy as the RFC decimal path.
    auto f1 = parse_copy("0x8000000000000000");
    REQUIRE(f1.root().is_float());
    REQUIRE(f1.root().as_float() == 9223372036854775808.0);
    auto f2 = parse_copy("0xFFFFFFFFFFFFFFFF");
    REQUIRE(f2.root().is_float());
    REQUIRE(f2.root().as_float() == 18446744073709551616.0);
    auto f3 = parse_copy("-0x8000000000000001");
    REQUIRE(f3.root().is_float());
    REQUIRE(f3.root().as_float() == -9223372036854775808.0);

    // Hex has no fraction or exponent part: the gap is an error.
    CHECK_THROWS_AS((void)parse_copy("0x1.5"), ParseError);
}

TEST_CASE("Json5: leading plus and decimal point variants")
{
    Json5ConfigGuard guard;
    Config::instance().set_json5(true);

    auto p1 = parse_copy("+1");
    REQUIRE(p1.root().is_int());
    REQUIRE(p1.root().as_int() == (int64_t)1);

    auto p2 = parse_copy("+0.5");
    REQUIRE(p2.root().is_float());
    REQUIRE(p2.root().as_float() == 0.5);

    auto l1 = parse_copy(".5");
    REQUIRE(l1.root().is_float());
    REQUIRE(l1.root().as_float() == 0.5);

    auto l2 = parse_copy("-.5");
    REQUIRE(l2.root().is_float());
    REQUIRE(l2.root().as_float() == -0.5);

    auto l3 = parse_copy("+.5");
    REQUIRE(l3.root().is_float());
    REQUIRE(l3.root().as_float() == 0.5);

    auto t1 = parse_copy("5.");
    REQUIRE(t1.root().is_float());
    REQUIRE(t1.root().as_float() == 5.0);

    auto t2 = parse_copy("5.e3");
    REQUIRE(t2.root().is_float());
    REQUIRE(t2.root().as_float() == 5000.0);

    auto t3 = parse_copy(".5e2");
    REQUIRE(t3.root().is_float());
    REQUIRE(t3.root().as_float() == 50.0);

    auto t4 = parse_copy("0.");
    REQUIRE(t4.root().is_float());
    REQUIRE(t4.root().as_float() == 0.0);

    // `+0` keeps the RFC int/double split (-0 is int 0); an explicit plus on
    // a 19-digit boundary still forces the double fallback.
    auto z = parse_copy("+0");
    REQUIRE(z.root().is_int());
    REQUIRE(z.root().as_int() == (int64_t)0);

    auto big = parse_copy("+9223372036854775808");
    REQUIRE(big.root().is_float());
    REQUIRE(big.root().as_float() == 9223372036854775808.0);
}

TEST_CASE("Json5: number rejections and default isolation")
{
    Json5ConfigGuard guard;
    Config &cfg = Config::instance();

    // Default RFC mode must reject every JSON5 number spelling.
    cfg.set_json5(false);
    CHECK_THROWS_AS((void)parse_copy("0x1"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("+1"), ParseError);
    CHECK_THROWS_AS((void)parse_copy(".5"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("5."), ParseError);
    CHECK_THROWS_AS((void)parse_copy("[0x1]"), ParseError);

    cfg.set_json5(true);

    auto expect_err = [](std::string_view input, size_t off)
    {
        try
        {
            (void)parse_copy(input);
            REQUIRE(false);
        }
        catch (const ParseError &e)
        {
            REQUIRE(e.category() == Category::Parse);
            REQUIRE(e.offset() == off);
        }
    };

    expect_err("0x", 2); // hex prefix with no digits
    expect_err("+0x", 3);
    expect_err(".", 1); // lone dot
    expect_err("+", 1); // lone sign
    expect_err("-", 1);
    expect_err("+.", 2);
    expect_err("1e", 2); // exponent with no digits
    expect_err("1e+", 3);
    expect_err(".e5", 1);
    expect_err("01", 2); // leading zeros stay illegal in JSON5
    expect_err("+01", 3);
    expect_err("-00", 3);

    // Hex beyond the finite-double range is a range error at token start.
    try
    {
        std::string huge = "0x1";
        huge.append(256, '0'); // 16^256 = 2^1024 > DBL_MAX
        (void)parse_copy(huge);
        REQUIRE(false);
    }
    catch (const ParseError &e)
    {
        REQUIRE(e.offset() == 0);
        REQUIRE(std::string(e.what()).find("Number out of double range") != std::string::npos);
    }
}

TEST_CASE("Json5: numbers in jsonl and RFC round-trip")
{
    Json5ConfigGuard guard;
    Config &cfg = Config::instance();

    cfg.set_json5(false);
    CHECK_THROWS_AS((void)parse_jsonl("0x10\n.5\n"), ParseError);

    cfg.set_json5(true);
    auto jl = parse_jsonl("0x10\n.5\n+2\n5.\n");
    REQUIRE(jl.root().size() == 4);
    REQUIRE(jl.root()[0].as_int() == (int64_t)16);
    REQUIRE(jl.root()[1].as_float() == 0.5);
    REQUIRE(jl.root()[2].as_int() == (int64_t)2);
    REQUIRE(jl.root()[3].as_float() == 5.0);

    // An unterminated hex prefix on one line must not swallow the next line
    // (same m_end discipline as the 40.1 trivia scans).
    CHECK_THROWS_AS((void)parse_jsonl("0x\n[1]\n"), ParseError);

    // Per-line range errors stay line-relative like the RFC path.
    std::string huge = "0x1";
    huge.append(256, '0');
    auto jr = parse_jsonl_result("0x10\n" + huge + "\n");
    REQUIRE(jr.is_err());
    REQUIRE(jr.unwrap_err().offset() == 0);

    // JSON5 number spellings normalize to RFC 8259 on dump.
    auto doc = parse_copy("{h:0x10, p:+2, l:.5, t:5.}");
    REQUIRE(sv(dump(doc)) == R"({"h":16,"p":2,"l":0.5,"t":5.0})");
}

TEST_CASE("Json5: borrowed keys survive in-situ and view shapes")
{
    Json5ConfigGuard guard;
    Config::instance().set_json5(true);

    std::pmr::string in_situ_buf("{name:'v'}");
    in_situ_buf.resize(in_situ_buf.size() + kPaddingWidth, '\0');
    auto in_situ = parse_in_situ(std::move(in_situ_buf));
    REQUIRE(in_situ.root()["name"].as_string() == std::string_view("v"));

    std::string view_buf("{name:'v'}");
    const size_t content_len = view_buf.size();
    view_buf.resize(view_buf.size() + kPaddingWidth, '\0');
    auto view = parse_view(view_buf.data(), content_len);
    REQUIRE(view.root()["name"].as_string() == std::string_view("v"));
}

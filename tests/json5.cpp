#include <doctest/doctest.h>

#include <cmath>
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

    // Explicit UTF-8 byte constants for the Unicode identifier / white-space
    // cases. Each multi-byte sequence is its own literal so a trailing digit
    // can never be absorbed by a greedy `\x` escape.
    const std::string kCafe = "caf\xC3\xA9";                         // café
    const std::string kAcute = "\xC3\xA9";                           // é (U+00E9)
    const std::string kOmega = "\xCE\xA9";                           // Ω (U+03A9)
    const std::string kCjk = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E"; // 日本語
    const std::string kCombining = "e\xCC\x81";                      // e + U+0301
    const std::string kArabicOne = "\xD9\xA1";                       // ١ (U+0661)
    const std::string kAstralA = "\xF0\x9D\x90\x80";                 // 𝐀 (U+1D400)
    const std::string kNbsp = "\xC2\xA0";                            // U+00A0
    const std::string kOgham = "\xE1\x9A\x80";                       // U+1680
    const std::string kEmSpace = "\xE2\x80\x83";                     // U+2003
    const std::string kLs = "\xE2\x80\xA8";                          // U+2028
    const std::string kPs = "\xE2\x80\xA9";                          // U+2029
    const std::string kNarrowNbsp = "\xE2\x80\xAF";                  // U+202F
    const std::string kMathSpace = "\xE2\x81\x9F";                   // U+205F
    const std::string kIdeoSpace = "\xE3\x80\x80";                   // U+3000
    const std::string kFeff = "\xEF\xBB\xBF";                        // U+FEFF
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

TEST_CASE("Json5: line continuations")
{
    Json5ConfigGuard guard;
    Config &cfg = Config::instance();
    cfg.set_json5(true);

    const std::string ls = "\xE2\x80\xA8"; // U+2028 LINE SEPARATOR
    const std::string ps = "\xE2\x80\xA9"; // U+2029 PARAGRAPH SEPARATOR

    // Backslash + each LineTerminator is removed entirely (no character).
    REQUIRE(parse_copy("\"a\\\nb\"").root().as_string() == std::string_view("ab"));
    REQUIRE(parse_copy("\"a\\\rb\"").root().as_string() == std::string_view("ab"));
    REQUIRE(parse_copy("\"a\\\r\nb\"").root().as_string() == std::string_view("ab"));
    REQUIRE(parse_copy("'a\\\nb'").root().as_string() == std::string_view("ab"));
    REQUIRE(parse_copy("'a\\\r\nb'").root().as_string() == std::string_view("ab"));
    REQUIRE(parse_copy("\"a\\" + ls + "b\"").root().as_string() == std::string_view("ab"));
    REQUIRE(parse_copy("\"a\\" + ps + "b\"").root().as_string() == std::string_view("ab"));
    REQUIRE(parse_copy("'a\\" + ls + "b'").root().as_string() == std::string_view("ab"));

    // A continuation immediately before the closing quote / several in a row.
    REQUIRE(parse_copy("\"a\\\n\"").root().as_string() == std::string_view("a"));
    REQUIRE(parse_copy("\"a\\\nb\\\nc\"").root().as_string() == std::string_view("abc"));

    // CR consumed on its own when the next byte is not LF.
    REQUIRE(parse_copy("\"a\\\rX\"").root().as_string() == std::string_view("aX"));

    // Default RFC mode rejects a backslash before a raw LineTerminator.
    cfg.set_json5(false);
    CHECK_THROWS_AS((void)parse_copy("\"a\\\nb\""), ParseError);
    CHECK_THROWS_AS((void)parse_copy("\"a\\\r\nb\""), ParseError);

    // parse_jsonl values are line sub-views: a continuation cannot pull the
    // next physical line into the current value; the line fails instead.
    cfg.set_json5(true);
    CHECK_THROWS_AS((void)parse_jsonl("\"a\\\nb\"\n"), ParseError);
}

TEST_CASE("Json5: hex, NUL/VT and identity escapes")
{
    Json5ConfigGuard guard;
    Config &cfg = Config::instance();
    cfg.set_json5(true);

    // \xHH in both quote styles.
    REQUIRE(parse_copy("\"\\x41\"").root().as_string() == std::string_view("A"));
    REQUIRE(parse_copy("'\\x41'").root().as_string() == std::string_view("A"));
    REQUIRE(parse_copy("'\\x4a'").root().as_string() == std::string_view("J"));
    REQUIRE(parse_copy("'\\x4A'").root().as_string() == std::string_view("J"));

    // \x00 and \0 both produce an embedded NUL byte.
    auto hx = parse_copy("'a\\x00b'");
    REQUIRE(hx.root().as_string().size() == 3);
    REQUIRE(hx.root().as_string()[1] == '\0');
    auto z = parse_copy("\"a\\0b\"");
    REQUIRE(z.root().as_string().size() == 3);
    REQUIRE(z.root().as_string()[1] == '\0');

    // \v and \' single escapes.
    const std::string v = vt();
    REQUIRE(parse_copy("'\\v'").root().as_string() == v);
    REQUIRE(parse_copy("\"\\v\"").root().as_string() == v);
    REQUIRE(parse_copy("'it\\'s'").root().as_string() == std::string_view("it's"));
    REQUIRE(parse_copy("\"\\'\"").root().as_string() == std::string_view("'"));

    // Identity escapes: any non-digit character yields itself.
    REQUIRE(parse_copy("'\\a'").root().as_string() == std::string_view("a"));
    REQUIRE(parse_copy("'\\z'").root().as_string() == std::string_view("z"));
    REQUIRE(parse_copy("'\\A'").root().as_string() == std::string_view("A"));
    REQUIRE(parse_copy("'\\!'").root().as_string() == std::string_view("!"));
    REQUIRE(parse_copy("'\\ '").root().as_string() == std::string_view(" "));
    REQUIRE(parse_copy("\"\\q\"").root().as_string() == std::string_view("q"));

    // The RFC escapes still decode through the same path.
    REQUIRE(parse_copy("'\\u0041\\n\\t'").root().as_string() == std::string_view("A\n\t"));

    // \1..\9 and \0<digit> stay rejected (no octal); bad \x rejected too.
    CHECK_THROWS_AS((void)parse_copy("'\\1'"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("'\\9'"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("'\\01'"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("'\\00'"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("'\\x'"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("'\\xG1'"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("'\\x1G'"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("'\\x1'"), ParseError);

    // Default RFC mode rejects every JSON5-only escape.
    cfg.set_json5(false);
    CHECK_THROWS_AS((void)parse_copy("\"\\x41\""), ParseError);
    CHECK_THROWS_AS((void)parse_copy("\"\\v\""), ParseError);
    CHECK_THROWS_AS((void)parse_copy("\"\\0\""), ParseError);
    CHECK_THROWS_AS((void)parse_copy("\"\\a\""), ParseError);
    CHECK_THROWS_AS((void)parse_copy("\"\\'\""), ParseError);
}

TEST_CASE("Json5: raw control characters in strings")
{
    Json5ConfigGuard guard;
    Config &cfg = Config::instance();

    cfg.set_json5(false);
    CHECK_THROWS_AS((void)parse_copy(std::string("\"a") + vt() + "b\""), ParseError);
    CHECK_THROWS_AS((void)parse_copy(std::string("\"a") + ff() + "b\""), ParseError);
    CHECK_THROWS_AS((void)parse_copy("\"a\tb\""), ParseError);

    cfg.set_json5(true);

    // VT/FF/TAB are not LineTerminators: legal raw in a JSON5 string.
    auto v = parse_copy(std::string("\"a") + vt() + "b\"");
    REQUIRE(v.root().as_string().size() == 3);
    REQUIRE(v.root().as_string()[1] == '\x0B');
    auto f = parse_copy(std::string("'a") + ff() + "b'");
    REQUIRE(f.root().as_string().size() == 3);
    REQUIRE(f.root().as_string()[1] == '\x0C');
    auto t = parse_copy("\"a\tb\"");
    REQUIRE(t.root().as_string().size() == 3);
    REQUIRE(t.root().as_string()[1] == '\t');

    // Raw LF/CR remain rejected (they are LineTerminators).
    CHECK_THROWS_AS((void)parse_copy(std::string_view("\"a\nb\"", 5)), ParseError);
    CHECK_THROWS_AS((void)parse_copy(std::string_view("'a\rb'", 5)), ParseError);

    // Raw NUL is a non-LineTerminator StringCharacter; the scalar decoder is
    // m_end-bounded rather than padding-terminated, so it is representable.
    const std::string raw_nul("\"a\0b\"", 5);
    auto n = parse_copy(raw_nul);
    REQUIRE(n.root().as_string().size() == 3);
    REQUIRE(n.root().as_string()[1] == '\0');
}

TEST_CASE("Json5: string escapes round-trip as RFC 8259")
{
    Json5ConfigGuard guard;
    Config &cfg = Config::instance();
    cfg.set_json5(true);

    const std::string expected_n("a\0b", 3);
    const std::string expected_v = vt();

    auto doc = parse_copy("{n:'a\\0b', v:'\\v', h:'\\x41', q:'it\\'s', c:'a\\x0bb'}");
    REQUIRE(doc.root()["n"].as_string() == std::string_view(expected_n));
    REQUIRE(doc.root()["v"].as_string() == expected_v);
    REQUIRE(doc.root()["h"].as_string() == std::string_view("A"));
    REQUIRE(doc.root()["q"].as_string() == std::string_view("it's"));
    REQUIRE(doc.root()["c"].as_string().size() == 3);

    const std::pmr::string dumped = dump(doc);
    REQUIRE(sv(dumped) ==
            std::string_view("{\"n\":\"a\\u0000b\",\"v\":\"\\u000b\",\"h\":\"A\",\"q\":\"it's\",\"c\":\"a\\u000bb\"}"));

    // The RFC dump re-parses in default mode to the same DOM.
    cfg.set_json5(false);
    auto rt = parse_copy(dumped);
    REQUIRE(rt.root() == doc.root());
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

TEST_CASE("Json5: Infinity and NaN literals")
{
    Json5ConfigGuard guard;
    Config &cfg = Config::instance();

    // Default RFC mode rejects every non-finite spelling (sign included).
    cfg.set_json5(false);
    CHECK_THROWS_AS((void)parse_copy("Infinity"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("+Infinity"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("-Infinity"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("NaN"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("[Infinity]"), ParseError);

    cfg.set_json5(true);

    // JSON5 1.0.0 §6: Infinity / NaN, each with an optional +/- sign.
    auto inf = parse_copy("Infinity");
    REQUIRE(inf.root().is_float());
    REQUIRE(std::isinf(inf.root().as_float()));
    REQUIRE(inf.root().as_float() > 0.0);

    auto pinf = parse_copy("+Infinity");
    REQUIRE(std::isinf(pinf.root().as_float()));
    REQUIRE(pinf.root().as_float() > 0.0);

    auto ninf = parse_copy("-Infinity");
    REQUIRE(std::isinf(ninf.root().as_float()));
    REQUIRE(ninf.root().as_float() < 0.0);

    REQUIRE(std::isnan(parse_copy("NaN").root().as_float()));
    REQUIRE(std::isnan(parse_copy("+NaN").root().as_float()));
    REQUIRE(std::isnan(parse_copy("-NaN").root().as_float()));

    // In containers, after JSON5 trivia, and mixed with finite numbers.
    auto arr = parse_copy("[Infinity, -Infinity, NaN, 1.5]");
    REQUIRE(arr.root().size() == 4);
    REQUIRE(std::isinf(arr.root()[0].as_float()));
    REQUIRE(arr.root()[1].as_float() < 0.0);
    REQUIRE(std::isnan(arr.root()[2].as_float()));
    REQUIRE(arr.root()[3].as_float() == 1.5);

    auto obj = parse_copy("{a:Infinity, /*c*/ b:-NaN}");
    REQUIRE(std::isinf(obj.root()["a"].as_float()));
    REQUIRE(std::isnan(obj.root()["b"].as_float()));

    REQUIRE(std::isinf(parse_copy("/*c*/ Infinity // tail").root().as_float()));

    // The spelling is exact and case-sensitive; trailing bytes are not part
    // of the literal and are rejected by the trailing/separator checks. The
    // matcher is m_end-bounded (never padding-terminated).
    CHECK_THROWS_AS((void)parse_copy("infinity"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("INFINITY"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("nan"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("NAN"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("Inf"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("Infinityx"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("Infinity0"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("NaNx"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("[Infinityx]"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("--Infinity"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("+-Infinity"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("++Infinity"), ParseError);

    // In key position they are ordinary ASCII identifiers, not literals.
    auto keys = parse_copy("{Infinity:1, NaN:2}");
    REQUIRE(keys.root()["Infinity"].as_int() == (int64_t)1);
    REQUIRE(keys.root()["NaN"].as_int() == (int64_t)2);
}

TEST_CASE("Json5: non-finite values in jsonl and line bounding")
{
    Json5ConfigGuard guard;
    Config &cfg = Config::instance();

    cfg.set_json5(false);
    CHECK_THROWS_AS((void)parse_jsonl("Infinity\n"), ParseError);

    cfg.set_json5(true);
    auto jl = parse_jsonl("Infinity\n-Infinity\nNaN\n");
    REQUIRE(jl.root().size() == 3);
    REQUIRE(std::isinf(jl.root()[0].as_float()));
    REQUIRE(jl.root()[1].as_float() < 0.0);
    REQUIRE(std::isnan(jl.root()[2].as_float()));

    // Trailing junk on one line must not swallow the next line (40.1 R1).
    CHECK_THROWS_AS((void)parse_jsonl("Infinityx\n[1]\n"), ParseError);

    // A non-finite value on its own line is fine, and the next line still
    // parses independently.
    auto jl2 = parse_jsonl("Infinity\n[1]\n");
    REQUIRE(jl2.root().size() == 2);
    REQUIRE(jl2.root()[1].size() == 1);
}

TEST_CASE("Json5: non-finite values cannot be dumped (RFC-only writer)")
{
    Json5ConfigGuard guard;
    Config::instance().set_json5(true);

    auto inf = parse_copy("Infinity");
    auto nan = parse_copy("NaN");
    REQUIRE(std::isinf(inf.root().as_float()));
    REQUIRE(std::isnan(nan.root().as_float()));

#ifndef __FAST_MATH__
    // No RFC 8259 spelling exists for inf/nan, so the writer records
    // NonFiniteDouble: the documented round-trip break. std::isfinite is
    // folded to true under -ffast-math, so this assertion is compiled out
    // there (the library itself never adds that flag).
    CHECK_THROWS_AS((void)dump(inf), JsonError);
    CHECK_THROWS_AS((void)dump(nan), JsonError);

    try
    {
        (void)dump(inf);
        REQUIRE(false);
    }
    catch (const JsonError &e)
    {
        REQUIRE(std::string(e.what()).find("non-finite") != std::string::npos);
    }

    auto r = dump_result(inf);
    REQUIRE(r.is_err());
    REQUIRE(r.unwrap_err().category() == Category::Json);
#else
    (void)inf;
    (void)nan;
#endif
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

TEST_CASE("Json5: Unicode identifier keys")
{
    Json5ConfigGuard guard;
    Config &cfg = Config::instance();
    cfg.set_json5(true);

    // Non-ASCII IdentifierStart: Latin-1, Greek, CJK, Cyrillic.
    auto cafe = parse_copy("{" + kCafe + ":1}");
    REQUIRE(cafe.root()[std::string_view(kCafe)].as_int() == (int64_t)1);
    REQUIRE(parse_copy("{" + kOmega + ":2}").root()[std::string_view(kOmega)].as_int() == (int64_t)2);
    REQUIRE(parse_copy("{" + kCjk + ":3}").root()[std::string_view(kCjk)].as_int() == (int64_t)3);
    REQUIRE(parse_copy("{\xD0\x9F\xD1\x80\xD0\xB8:4}").root()["\xD0\x9F\xD1\x80\xD0\xB8"].as_int() == (int64_t)4);

    // Non-ASCII IdentifierPart: combining mark (Mn) and decimal digit (Nd).
    auto comb = parse_copy("{" + kCombining + ":5}");
    REQUIRE(comb.root()[std::string_view(kCombining)].as_int() == (int64_t)5);
    auto arab = parse_copy("{x" + kArabicOne + ":6}");
    REQUIRE(arab.root()[std::string_view("x" + kArabicOne)].as_int() == (int64_t)6);

    // Mixed ASCII prefix and Unicode continuation.
    auto mixed = parse_copy("{a" + kAcute + ":7}");
    REQUIRE(mixed.root()[std::string_view("a" + kAcute)].as_int() == (int64_t)7);

    // Classification is by Unicode general category, not by script block:
    // U+00AA (ª, Lo) and U+00B5 (µ, Ll) are letters, while U+00D7 (×, Sm)
    // and U+00B2 (², No) are not.
    REQUIRE(parse_copy("{\xC2\xAA:1}").root()["\xC2\xAA"].as_int() == (int64_t)1);
    REQUIRE(parse_copy("{\xC2\xB5:2}").root()["\xC2\xB5"].as_int() == (int64_t)2);
    CHECK_THROWS_AS((void)parse_copy("{\xC3\x97:1}"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("{\xC2\xB2:1}"), ParseError);

    // \uXXXX escapes in any position, decoded in place.
    REQUIRE(parse_copy("{\\u0061:1}").root()["a"].as_int() == (int64_t)1);
    REQUIRE(parse_copy("{\\u65E5\\u672C\\u8A9E:2}").root()[std::string_view(kCjk)].as_int() == (int64_t)2);
    REQUIRE(parse_copy("{caf\\u00E9:3}").root()[std::string_view(kCafe)].as_int() == (int64_t)3);
    REQUIRE(parse_copy("{a\\u0062c:4}").root()["abc"].as_int() == (int64_t)4);
    REQUIRE(parse_copy("{e\\u0301:5}").root()[std::string_view(kCombining)].as_int() == (int64_t)5);
    // Surrogate pair combines into one astral UnicodeLetter (U+1D400, Lu).
    REQUIRE(parse_copy("{\\uD835\\uDC00:6}").root()[std::string_view(kAstralA)].as_int() == (int64_t)6);
    // ZWNJ (U+200C) is an explicit IdentifierPart character.
    REQUIRE(parse_copy("{a\\u200Cb:7}")
                .root()["a\xE2\x80\x8C"
                        "b"]
                .as_int() == (int64_t)7);

    // An escaped key and its quoted twin are the same DOM.
    REQUIRE(parse_copy("{\\u0061:1}").root() == parse_copy("{\"a\":1}").root());

    // Rejections: an escape that is not a valid identifier character ends the
    // name (maximal munch), and a non-letter non-ASCII code point is not a key.
    CHECK_THROWS_AS((void)parse_copy("{\\u0030:1}"), ParseError);   // digit as start
    CHECK_THROWS_AS((void)parse_copy("{a\\u0020b:1}"), ParseError); // space in continuation
    CHECK_THROWS_AS((void)parse_copy("{\\u00:1}"), ParseError);     // truncated escape
    CHECK_THROWS_AS((void)parse_copy("{\\uXXXX:1}"), ParseError);   // non-hex digits
    CHECK_THROWS_AS((void)parse_copy("{\\uD83D:1}"), ParseError);   // lone high surrogate
    CHECK_THROWS_AS((void)parse_copy("{\\uD83Dx:1}"), ParseError);  // high not followed by \u
    CHECK_THROWS_AS((void)parse_copy("{\\uDE00:1}"), ParseError);   // lone low surrogate
    CHECK_THROWS_AS((void)parse_copy("{\xC2\xA9:1}"), ParseError);  // © (So), not a letter
    // Malformed UTF-8 is never an identifier character (independently of the
    // raw-string-only strict_utf8 knob).
    CHECK_THROWS_AS((void)parse_copy("{a\xFF:1}"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("{a\xE2\x80:1}"), ParseError);

    // Default RFC mode rejects both raw and escaped Unicode keys.
    cfg.set_json5(false);
    CHECK_THROWS_AS((void)parse_copy("{" + kCafe + ":1}"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("{\\u0061:1}"), ParseError);
}

TEST_CASE("Json5: Unicode identifiers and strict duplicate keys")
{
    Json5ConfigGuard guard;
    Config &cfg = Config::instance();
    cfg.set_json5(true);

    // Unicode and escaped spellings compare by decoded content.
    cfg.set_strict_duplicate_keys(true);
    CHECK_THROWS_AS((void)parse_copy("{" + kCafe + ":1,\"" + kCafe + "\":2}"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("{\\u0061:1,a:2}"), ParseError);
    cfg.set_strict_duplicate_keys(false);
    REQUIRE(parse_copy("{\\u0061:1,a:2}").root()["a"].as_int() == (int64_t)2);
}

TEST_CASE("Json5: escaped identifier keys decode in place")
{
    Json5ConfigGuard guard;
    Config::instance().set_json5(true);

    // The key borrows the input buffer; an escaped key is decoded in place
    // (escaped spelling is longer than the decoded UTF-8), like strings.
    std::pmr::string in_situ_buf("{caf\\u00E9:'v'}");
    in_situ_buf.resize(in_situ_buf.size() + kPaddingWidth, '\0');
    auto in_situ = parse_in_situ(std::move(in_situ_buf));
    REQUIRE(in_situ.root()[std::string_view(kCafe)].as_string() == std::string_view("v"));

    std::string view_buf("{caf\\u00E9:'v'}");
    const size_t content_len = view_buf.size();
    view_buf.resize(view_buf.size() + kPaddingWidth, '\0');
    auto view = parse_view(view_buf.data(), content_len);
    REQUIRE(view.root()[std::string_view(kCafe)].as_string() == std::string_view("v"));

    REQUIRE(sv(dump(parse_copy("{caf\\u00E9:'x'}"))) == "{\"caf\xC3\xA9\":\"x\"}");
}

TEST_CASE("Json5: Unicode whitespace")
{
    Json5ConfigGuard guard;
    Config &cfg = Config::instance();

    const std::string all[] = {kNbsp, kOgham, kEmSpace, kLs, kPs, kNarrowNbsp, kMathSpace, kIdeoSpace, kFeff};

    // Default RFC rejects every JSON5-only white space code point.
    cfg.set_json5(false);
    for (const std::string &ws : all)
        CHECK_THROWS_AS((void)parse_copy(ws + "1"), ParseError);

    cfg.set_json5(true);

    // Leading / trailing trivia around a value.
    for (const std::string &ws : all)
    {
        REQUIRE(parse_copy(ws + "1").root().as_int() == (int64_t)1);
        REQUIRE(parse_copy("1" + ws).root().as_int() == (int64_t)1);
    }

    // Between every token of an object.
    auto obj = parse_copy("{" + kNbsp + "\"a\"" + kEmSpace + ":" + kIdeoSpace + "1" + kFeff + "}");
    REQUIRE(obj.root()["a"].as_int() == (int64_t)1);

    // The JSON5 literal trailing-byte gate is relaxed, so Unicode white space
    // (and comments) may follow `true`/`false`/`null`.
    REQUIRE(parse_copy("true" + kNbsp).root().as_boolean() == true);
    REQUIRE(parse_copy("[" + kFeff + "true" + kLs + ",1]").root().size() == 2);

    // A line comment ends at U+2028 / U+2029 (JSON5 LineTerminator set).
    REQUIRE(parse_copy("[1, //c" + kLs + "2]").root().size() == 2);
    REQUIRE(parse_copy("[1, //c" + kPs + "2]").root().size() == 2);
    REQUIRE(parse_copy("[1, //c" + kLs + "2]").root()[1].as_int() == (int64_t)2);

    // A non-whitespace non-ASCII code point is not trivia; a malformed or
    // truncated multi-byte sequence is never skipped as white space.
    CHECK_THROWS_AS((void)parse_copy(kAcute + "1"), ParseError);
    CHECK_THROWS_AS((void)parse_copy("\xC2\x85"
                                     "1"),
                    ParseError); // NEL (Cc), not Zs
    CHECK_THROWS_AS((void)parse_copy(std::string("\xC2", 1) + "1"), ParseError);
    CHECK_THROWS_AS((void)parse_copy(kLs.substr(0, 2) + "1"), ParseError);
}

TEST_CASE("Json5: Unicode whitespace in jsonl and line bounding")
{
    Json5ConfigGuard guard;
    Config &cfg = Config::instance();

    cfg.set_json5(false);
    CHECK_THROWS_AS((void)parse_jsonl(kNbsp + "\n[1]\n"), ParseError);

    cfg.set_json5(true);

    // A Unicode-whitespace-only line is blank and must be skipped, never
    // handed to a per-line parser (R1: no line may be swallowed).
    for (const std::string &ws : {kNbsp, kEmSpace, kOgham, kIdeoSpace, kFeff})
    {
        auto doc = parse_jsonl(ws + "\n[1]\n");
        REQUIRE(doc.root().size() == 1);
        REQUIRE(doc.root()[0].size() == 1);
        REQUIRE(doc.root()[0][0].as_int() == (int64_t)1);
    }

    // A non-whitespace byte after the Unicode white space makes the line
    // non-blank: the parser reports on that line instead of skipping it.
    CHECK_THROWS_AS((void)parse_jsonl(kNbsp + "x\n[1]\n"), ParseError);

    // U+2028 is not a jsonl line separator: it stays on the physical line and
    // terminates the line comment there, so `[3]` still parses independently.
    auto doc = parse_jsonl("[1, //c" + kLs + "2]\n[3]\n");
    REQUIRE(doc.root().size() == 2);
    REQUIRE(doc.root()[0].size() == 2);
    REQUIRE(doc.root()[1][0].as_int() == (int64_t)3);
}

TEST_CASE("Json5: Unicode input dumps as RFC 8259")
{
    Json5ConfigGuard guard;
    Config::instance().set_json5(true);

    auto doc = parse_copy("{caf\\u00E9:'x', " + kOmega + ":1, \\u65E5\\u672C:2}");
    REQUIRE(sv(dump(doc)) == "{\"caf\xC3\xA9\":\"x\",\""
                             "\xCE\xA9"
                             "\":1,\""
                             "\xE6\x97\xA5\xE6\x9C\xAC"
                             "\":2}");
}

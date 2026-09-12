#include <doctest/doctest.h>
#include <ostream>

#include "pjh_json/config.hpp"
#include "pjh_json/document.hpp"
#include "pjh_json/error.hpp"
#include "pjh_json/json_constexpr.hpp" // ConstJson::parse (consteval)

#include <string>
#include <string_view>

using namespace pjh::json;

namespace
{
    // One corpus entry: the SAME input is fed to the compile-time validator
    // (ConstJson::parse, via the macro) and to the runtime parser (parse_copy).
    // `divergence == nullptr` means the two verdicts MUST agree.
    struct diff_case
    {
        std::string_view input;
        bool expect_consteval;
        bool expect_runtime;
        const char *divergence; // nullptr = must agree
        bool consteval_ok;      // computed at compile time by the macro
    };

    // The macro evaluates ConstJson::parse in a constant expression, so the
    // consteval verdict is pinned at compile time. Any drift in validate.hpp
    // (or in the table) breaks the static_asserts below.
    //
    // Greedy-hex caveat: string literals with a hex escape followed by a hex
    // digit must use adjacent-string concatenation (`"\x0b" "1"`), otherwise
    // the escape swallows the digit ("\x0b1" is the single byte 0xB1).
#define PJH_DIFF_CASE(text, ce, rt, div) \
    diff_case { text, ce, rt, div, ConstJson::parse(text).valid }

    constexpr diff_case kCases[] = {
        // ----- literals ----------------------------------------------------
        PJH_DIFF_CASE("null", true, true, nullptr),
        PJH_DIFF_CASE("true", true, true, nullptr),
        PJH_DIFF_CASE("false", true, true, nullptr),
        PJH_DIFF_CASE("nul", false, false, nullptr),
        PJH_DIFF_CASE("truex", false, false, nullptr),
        // ----- numbers: agreements ----------------------------------------
        PJH_DIFF_CASE("0", true, true, nullptr),
        PJH_DIFF_CASE("-0", true, true, nullptr),
        PJH_DIFF_CASE("42", true, true, nullptr),
        PJH_DIFF_CASE("-42", true, true, nullptr),
        PJH_DIFF_CASE("9223372036854775807", true, true, nullptr),
        PJH_DIFF_CASE("-9223372036854775808", true, true, nullptr),
        PJH_DIFF_CASE("18446744073709551615", true, true, nullptr),
        PJH_DIFF_CASE("0.5", true, true, nullptr),
        PJH_DIFF_CASE("1e308", true, true, nullptr),
        PJH_DIFF_CASE("0e400", true, true, nullptr),
        PJH_DIFF_CASE("1e-320", true, true, nullptr),
        PJH_DIFF_CASE("01", false, false, nullptr),
        PJH_DIFF_CASE("-01", false, false, nullptr),
        PJH_DIFF_CASE("1.", false, false, nullptr),
        PJH_DIFF_CASE("1e", false, false, nullptr),
        PJH_DIFF_CASE("-", false, false, nullptr),
        PJH_DIFF_CASE("+1", false, false, nullptr),
        // ----- strings / escapes / surrogates -----------------------------
        PJH_DIFF_CASE(R"("")", true, true, nullptr),
        PJH_DIFF_CASE(R"("a\nb")", true, true, nullptr),
        PJH_DIFF_CASE(R"("\u0041")", true, true, nullptr),
        PJH_DIFF_CASE(R"("\uD83D\uDE00")", true, true, nullptr),
        PJH_DIFF_CASE(R"("\uD800")", false, false, nullptr),
        PJH_DIFF_CASE(R"("\uDC00")", false, false, nullptr),
        PJH_DIFF_CASE(R"("\uD800\u0041")", false, false, nullptr),
        PJH_DIFF_CASE(R"("\q")", false, false, nullptr),
        PJH_DIFF_CASE(R"("unterminated)", false, false, nullptr),
        // ----- raw bytes (default config: byte mirror) --------------------
        PJH_DIFF_CASE(std::string_view("\"a\nb\"", 5), false, false, nullptr), // raw LF
        PJH_DIFF_CASE(std::string_view("\"a\x00"
                                       "b\"",
                                       5),
                      false, false, nullptr), // raw NUL
        PJH_DIFF_CASE(std::string_view("\"a\x7f"
                                       "b\"",
                                       5),
                      true, true, nullptr), // DEL legal
        PJH_DIFF_CASE(std::string_view("\"a\xFF"
                                       "b\"",
                                       5),
                      true, true, nullptr), // bad UTF-8 both accept
        // ----- containers / whitespace ------------------------------------
        PJH_DIFF_CASE("[]", true, true, nullptr),
        PJH_DIFF_CASE("{}", true, true, nullptr),
        PJH_DIFF_CASE("[1,2,3]", true, true, nullptr),
        PJH_DIFF_CASE(R"({"a":1})", true, true, nullptr),
        PJH_DIFF_CASE("[1,]", false, false, nullptr),
        PJH_DIFF_CASE("{\"a\":}", false, false, nullptr),
        PJH_DIFF_CASE("{} {}", false, false, nullptr),
        PJH_DIFF_CASE(" \t\r\n 42 \t", true, true, nullptr),
        PJH_DIFF_CASE(std::string_view("\x0b"
                                       "1",
                                       2),
                      false, false, nullptr), // VT not WS
        PJH_DIFF_CASE(std::string_view("1\x0b", 2), false, false, nullptr),
        // ----- BOM: both reject at DEFAULT config -------------------------
        PJH_DIFF_CASE(std::string_view("\xEF\xBB\xBF"
                                       "1",
                                       4),
                      false, false, nullptr),
        // ----- documented divergence: number range ------------------------
        PJH_DIFF_CASE("1e400", true, false, "number range"),
        PJH_DIFF_CASE("1e-400", true, false, "number range"),
        PJH_DIFF_CASE("1e309", true, false, "number range"),
    };

#undef PJH_DIFF_CASE

    // Compile-time table verification: pins both verdicts and the
    // agree-or-diverge contract. A one-sided change to either validator
    // breaks the build instead of silently weakening the guard.
    consteval bool table_is_consistent()
    {
        for (const auto &c : kCases)
        {
            if (c.consteval_ok != c.expect_consteval)
                return false;
            if ((c.divergence == nullptr) != (c.expect_consteval == c.expect_runtime))
                return false;
        }
        return true;
    }
    static_assert(table_is_consistent(),
                  "differential table: consteval verdict / allowlist out of sync");

    // Canary: the number-range case really is consteval-true / runtime-false.
    static_assert(ConstJson::parse("1e400").valid, "1e400 must be grammatically valid (number-range canary)");
    static_assert(!ConstJson::parse(std::string_view("\xEF\xBB\xBF" "1", 4)).valid,
                  "BOM must be rejected by the consteval grammar");
} // namespace

TEST_CASE("Differential: consteval verdict vs runtime parse (default config)")
{
    // Establish the documented default policies explicitly (other tests may
    // have toggled them; ConfigGuard elsewhere restores, this is belt-and-
    // braces). max_depth default stays 512.
    Config &cfg = Config::instance();
    const bool saved_bom = cfg.strip_bom();
    const bool saved_utf8 = cfg.strict_utf8();
    const bool saved_json5 = cfg.json5();
    cfg.set_strip_bom(false);
    cfg.set_strict_utf8(false);
    cfg.set_json5(false);

    for (const auto &c : kCases)
    {
        bool rt_ok = true;
        try
        {
            Document doc = parse_copy(c.input);
            (void)doc;
        }
        catch (const ParseError &)
        {
            rt_ok = false;
        }

        INFO("input: " << std::string(c.input));
        CHECK(rt_ok == c.expect_runtime);
        if (c.divergence == nullptr)
            CHECK(c.consteval_ok == rt_ok);
        else
            CHECK(c.consteval_ok != rt_ok);
    }

    cfg.set_strip_bom(saved_bom);
    cfg.set_strict_utf8(saved_utf8);
    cfg.set_json5(saved_json5);
}

TEST_CASE("Differential: documented divergences under opt-in knobs")
{
    Config &cfg = Config::instance();
    const bool saved_bom = cfg.strip_bom();
    const bool saved_utf8 = cfg.strict_utf8();
    const bool saved_json5 = cfg.json5();
    cfg.set_json5(false);

    // BOM divergence: runtime strips, consteval stays grammar-strict.
    cfg.set_strip_bom(true);
    cfg.set_strict_utf8(false);
    static_assert(!ConstJson::parse(std::string_view("\xEF\xBB\xBF" "1", 4)).valid,
                  "consteval stays BOM-strict");
    REQUIRE(parse_copy(std::string_view("\xEF\xBB\xBF" "1", 4)).root().as_int() == 1);

    // strict_utf8 divergence: runtime rejects raw bad UTF-8, consteval is
    // raw-byte-lenient by construction.
    cfg.set_strip_bom(false);
    cfg.set_strict_utf8(true);
    static_assert(ConstJson::parse(std::string_view("\"a\xFF" "b\"", 5)).valid,
                  "consteval stays raw-byte-lenient");
    CHECK_THROWS_AS((void)parse_copy(std::string_view("\"a\xFF" "b\"", 5)), ParseError);

    // Control: legal UTF-8 passes both under strict_utf8 ON.
    REQUIRE(parse_copy(std::string_view("\"caf\xC3\xA9\"", 7)).root().as_string() ==
            std::string_view("caf\xC3\xA9", 5));

    cfg.set_strip_bom(saved_bom);
    cfg.set_strict_utf8(saved_utf8);
    cfg.set_json5(saved_json5);
}

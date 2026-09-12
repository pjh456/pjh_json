// Task 79.5 — differential error channel golden table.
//
// Golden error table captured BEFORE task 79.1 from the unmodified working
// tree (HEAD 5bf6604, Debug, PJH_JSON_ENABLE_OPTIMIZATIONS=OFF, g++ 14.2.0),
// archived at .w1mer/results/79_golden_errors.md §A. The `what` strings below
// are byte-identical to that capture. File-I/O rows use portable relative
// missing paths (plan 79.5 §2.1); their `what` is synthesised from
// {code, detail}. Every row is checked against the throwing channel, the
// *_result channel, and Error{code,...}.format() (the static message table).
//
// The pre-79 gate has landed (79.4): both public shells forward to one
// zero-throw *_impl, so `same_tuple` guards against a future per-shell
// post-processing drift, while the golden bytes guard the pre-79 text.
#include <doctest/doctest.h>
#include <ostream> // MSVC doctest stringification (ebf47c4 precedent)

#include <pjh_result/diagnostic.hpp>

#include <pjh_json/array.hpp>
#include <pjh_json/config.hpp>
#include <pjh_json/document.hpp>
#include <pjh_json/error.hpp>
#include <pjh_json/json.hpp>
#include <pjh_json/parser.hpp>
#include <pjh_json/writer.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>

using namespace pjh::json;

// ---------------------------------------------------------------------------
// Compile-time kernel properties (plan 79 §3.3 / §6)
// ---------------------------------------------------------------------------
static_assert(std::is_trivially_copyable_v<Error>);
static_assert(std::is_nothrow_move_constructible_v<Error>);
static_assert(pjh::result::Diagnostic<Error>);

namespace
{
    // -----------------------------------------------------------------------
    // Observed tuple: {dynamic type, Category, offset(), what()} per channel
    // -----------------------------------------------------------------------
    struct observed
    {
        bool        threw = false;          // channel reported failure
        bool        is_parse_error = false; // dynamic type ParseError vs JsonError
        Category    category = Category::Json;
        bool        has_offset = false;     // ParseError::offset() exists
        size_t      offset = 0;
        std::string what;                   // full what(), byte-exact
    };

    template <class F>
    observed observe_throw(F &&fn)
    {
        observed o;
        try
        {
            fn();
        }
        catch (const ParseError &e) // must precede JsonError (derived)
        {
            o.threw = true;
            o.is_parse_error = true;
            o.category = e.category();
            o.has_offset = true;
            o.offset = e.offset();
            o.what = e.what();
        }
        catch (const JsonError &e) // writer base / TypeError
        {
            o.threw = true;
            o.is_parse_error = (dynamic_cast<const ParseError *>(&e) != nullptr);
            o.category = e.category();
            o.has_offset = false;
            o.offset = 0;
            o.what = e.what();
        }
        return o;
    }

    observed observe_result(pjh::result::Result<Document, ParseError> r)
    {
        observed o;
        if (r.is_ok())
            return o;
        ParseError e = std::move(r).unwrap_err();
        o.threw = true;
        o.is_parse_error = true;
        o.category = e.category();
        o.has_offset = true;
        o.offset = e.offset();
        o.what = e.what();
        return o;
    }

    observed observe_result(pjh::result::Result<std::pmr::string, JsonError> r)
    {
        observed o;
        if (r.is_ok())
            return o;
        JsonError e = std::move(r).unwrap_err();
        o.threw = true;
        o.is_parse_error = false;
        o.category = e.category();
        o.has_offset = false;
        o.offset = 0;
        o.what = e.what();
        return o;
    }

    bool same_tuple(const observed &a, const observed &b)
    {
        return a.threw == b.threw && a.is_parse_error == b.is_parse_error &&
               a.category == b.category && a.has_offset == b.has_offset &&
               (!a.has_offset || a.offset == b.offset) && a.what == b.what;
    }

    // -----------------------------------------------------------------------
    // Golden table
    // -----------------------------------------------------------------------
    enum class Group
    {
        ParseDefault,  // parse_copy, all knobs at defaults
        ParseKnobbed,  // parse_copy with per-row Config knobs
        ContextFree,   // insitu / file / stream / parse_view (bespoke driver)
        Jsonl,         // parse_jsonl
        Writer,        // dump family (bespoke driver)
    };

    struct golden_row
    {
        const char      *label;
        Group            group;
        ErrorCode        code = ErrorCode::None; // None == OK row
        bool             parse_error = true;     // dynamic exception type
        Category         category = Category::Parse;
        size_t           offset = 0;             // golden offset()
        bool             positioned = false;     // what() carries " at offset N"
        std::string_view what;
        std::string_view input{}; // default keeps -Wmissing-field-initializers quiet
        bool             has_result = true;      // false: throwing-only row
        std::string_view detail{};               // borrowed "{}" payload
        bool             strict_dup = false;
        bool             strict_utf8 = false;
        bool             strip_bom = false;
        size_t           max_depth = Config::kDefaultMaxDepth;
    };

    constexpr golden_row kAllRows[] = {
        // ---- A. parse_copy, default knobs --------------------------------
        {.label = "parse.extra_chars",
         .group = Group::ParseDefault,
         .code = ErrorCode::ExtraCharactersAfterValue,
         .offset = 2,
         .positioned = true,
         .what = "Extra characters after complete JSON value at offset 2",
         .input = "1 2"},
        {.label = "parse.unexpected_value_char_top",
         .group = Group::ParseDefault,
         .code = ErrorCode::UnexpectedValueCharacter,
         .offset = 0,
         .positioned = true,
         .what = "Unexpected character parsing value at offset 0",
         .input = "x"},
        {.label = "parse.unexpected_char_inplace",
         .group = Group::ParseDefault,
         .code = ErrorCode::UnexpectedCharacter,
         .offset = 1,
         .positioned = true,
         .what = "Unexpected character at offset 1",
         .input = "[x]"},
        {.label = "parse.unexpected_end_of_input",
         .group = Group::ParseDefault,
         .code = ErrorCode::UnexpectedEndOfInput,
         .offset = 0,
         .positioned = true,
         .what = "Unexpected end of input at offset 0",
         .input = ""},
        {.label = "parse.unexpected_end_inplace",
         .group = Group::ParseDefault,
         .code = ErrorCode::UnexpectedCharacter,
         .offset = 3,
         .positioned = true,
         .what = "Unexpected character at offset 3",
         .input = "[1,"},
        {.label = "parse.bom.default_rejected",
         .group = Group::ParseDefault,
         .code = ErrorCode::UnexpectedValueCharacter,
         .offset = 0,
         .positioned = true,
         .what = "Unexpected character parsing value at offset 0",
         .input = "\xEF\xBB\xBF{}"},
        {.label = "object.expected_key",
         .group = Group::ParseDefault,
         .code = ErrorCode::ExpectedStringKey,
         .offset = 1,
         .positioned = true,
         .what = "Expected string key in object at offset 1",
         .input = "{1:2}"},
        {.label = "object.expected_colon",
         .group = Group::ParseDefault,
         .code = ErrorCode::ExpectedColon,
         .offset = 5,
         .positioned = true,
         .what = "Expected ':' in object at offset 5",
         .input = "{\"a\" 1}"},
        {.label = "object.unexpected_end",
         .group = Group::ParseDefault,
         .code = ErrorCode::UnexpectedEndOfObject,
         .offset = 6,
         .positioned = true,
         .what = "Unexpected end of object at offset 6",
         .input = "{\"a\":1"},
        {.label = "object.expected_comma_brace",
         .group = Group::ParseDefault,
         .code = ErrorCode::ExpectedCommaOrBrace,
         .offset = 7,
         .positioned = true,
         .what = "Expected ',' or '}' in object at offset 7",
         .input = "{\"a\":1 \"b\":2}"},
        {.label = "array.expected_comma_bracket",
         .group = Group::ParseDefault,
         .code = ErrorCode::ExpectedCommaOrBracket,
         .offset = 3,
         .positioned = true,
         .what = "Expected ',' or ']' in array at offset 3",
         .input = "[1 2]"},
        {.label = "number.no_int_digits",
         .group = Group::ParseDefault,
         .code = ErrorCode::NumberNoIntDigits,
         .offset = 1,
         .positioned = true,
         .what = "Invalid number: no digits after '-' at offset 1",
         .input = "-"},
        {.label = "number.leading_zero",
         .group = Group::ParseDefault,
         .code = ErrorCode::NumberLeadingZero,
         .offset = 2,
         .positioned = true,
         .what = "Invalid number: leading zeros are not allowed at offset 2",
         .input = "01"},
        {.label = "number.no_frac_digits",
         .group = Group::ParseDefault,
         .code = ErrorCode::NumberNoFracDigits,
         .offset = 2,
         .positioned = true,
         .what = "Invalid number: no digits after decimal point at offset 2",
         .input = "1."},
        {.label = "number.no_exp_digits",
         .group = Group::ParseDefault,
         .code = ErrorCode::NumberNoExpDigits,
         .offset = 2,
         .positioned = true,
         .what = "Invalid number: no digits in exponent at offset 2",
         .input = "1e"},
        {.label = "number.out_of_range",
         .group = Group::ParseDefault,
         .code = ErrorCode::NumberOutOfRange,
         .offset = 0,
         .positioned = true,
         .what = "Number out of double range at offset 0",
         .input = "1e999"},
        {.label = "string.unterminated",
         .group = Group::ParseDefault,
         .code = ErrorCode::UnterminatedString,
         .offset = 4,
         .positioned = true,
         .what = "Unterminated string at offset 4",
         .input = "\"abc"},
        // Adjacent literals: avoid the greedy `\xNN` hex trap (0x01 + 'b').
        {.label = "string.unescaped_control",
         .group = Group::ParseDefault,
         .code = ErrorCode::UnescapedControl,
         .offset = 2,
         .positioned = true,
         .what = "Unescaped control character in string at offset 2",
         .input = "\"a\x01" "b\""},
        {.label = "string.invalid_escape",
         .group = Group::ParseDefault,
         .code = ErrorCode::InvalidEscapeChar,
         .offset = 3,
         .positioned = true,
         .what = "Invalid escape character at offset 3",
         .input = "\"a\\x\""},
        {.label = "string.invalid_hex",
         .group = Group::ParseDefault,
         .code = ErrorCode::InvalidHexDigit,
         .offset = 3,
         .positioned = true,
         .what = "Invalid hex digit in unicode escape at offset 3",
         .input = "\"\\uZZZZ\""},
        {.label = "string.invalid_surrogate_pair",
         .group = Group::ParseDefault,
         .code = ErrorCode::InvalidSurrogatePair,
         .offset = 13,
         .positioned = true,
         .what = "Invalid surrogate pair at offset 13",
         .input = "\"\\uD800\\u0041\""},
        {.label = "string.expected_low_surrogate",
         .group = Group::ParseDefault,
         .code = ErrorCode::ExpectedLowSurrogate,
         .offset = 7,
         .positioned = true,
         .what = "Expected low surrogate at offset 7",
         .input = "\"\\uD800x\""},
        {.label = "string.lone_low_surrogate",
         .group = Group::ParseDefault,
         .code = ErrorCode::LoneLowSurrogate,
         .offset = 7,
         .positioned = true,
         .what = "Lone low surrogate, expected high surrogate first at offset 7",
         .input = "\"\\uDC00\""},
        {.label = "literal.invalid",
         .group = Group::ParseDefault,
         .code = ErrorCode::InvalidLiteral,
         .offset = 0,
         .positioned = true,
         .what = "Invalid literal, expected true/false/null at offset 0",
         .input = "tru"},
        {.label = "literal.trailing",
         .group = Group::ParseDefault,
         .code = ErrorCode::InvalidLiteralTrailing,
         .offset = 4,
         .positioned = true,
         .what = "Invalid literal, unexpected characters after true/false/null at offset 4",
         .input = "truex"},

        // ---- B. parse_copy with knobs ------------------------------------
        {.label = "parse.bom.strip_ok",
         .group = Group::ParseKnobbed,
         .code = ErrorCode::None,
         .what = "",
         .input = "\xEF\xBB\xBF{}",
         .strip_bom = true},
        {.label = "object.duplicate_key",
         .group = Group::ParseKnobbed,
         .code = ErrorCode::DuplicateKey,
         .offset = 0,
         .what = "Duplicate key \"a\" in object",
         .input = "{\"a\":1,\"a\":2}",
         .detail = "a",
         .strict_dup = true},
        {.label = "object.duplicate_key_empty",
         .group = Group::ParseKnobbed,
         .code = ErrorCode::DuplicateKey,
         .offset = 0,
         .what = "Duplicate key \"\" in object",
         .input = "{\"\":1,\"\":2}",
         .detail = "",
         .strict_dup = true},
        {.label = "array.max_depth",
         .group = Group::ParseKnobbed,
         .code = ErrorCode::MaxDepthExceeded,
         .offset = 1,
         .positioned = true,
         .what = "Maximum nesting depth exceeded at offset 1",
         .input = "[[]]",
         .max_depth = 1},
        {.label = "object.max_depth",
         .group = Group::ParseKnobbed,
         .code = ErrorCode::MaxDepthExceeded,
         .offset = 5,
         .positioned = true,
         .what = "Maximum nesting depth exceeded at offset 5",
         .input = "{\"a\":{}}",
         .max_depth = 1},
        {.label = "utf8.overlong_c0.on",
         .group = Group::ParseKnobbed,
         .code = ErrorCode::Utf8Overlong,
         .offset = 2,
         .positioned = true,
         .what = "Overlong UTF-8 sequence in string at offset 2",
         .input = "\"a\xC0\x80\"",
         .strict_utf8 = true},
        {.label = "utf8.overlong_e0.on",
         .group = Group::ParseKnobbed,
         .code = ErrorCode::Utf8Overlong,
         .offset = 2,
         .positioned = true,
         .what = "Overlong UTF-8 sequence in string at offset 2",
         .input = "\"a\xE0\x80\x80\"",
         .strict_utf8 = true},
        {.label = "utf8.invalid_lead_ff.on",
         .group = Group::ParseKnobbed,
         .code = ErrorCode::Utf8InvalidLead,
         .offset = 2,
         .positioned = true,
         .what = "Invalid UTF-8 lead byte in string at offset 2",
         .input = "\"a\xFF\"",
         .strict_utf8 = true},
        {.label = "utf8.invalid_lead_f5.on",
         .group = Group::ParseKnobbed,
         .code = ErrorCode::Utf8InvalidLead,
         .offset = 2,
         .positioned = true,
         .what = "Invalid UTF-8 lead byte in string at offset 2",
         .input = "\"a\xF5\"",
         .strict_utf8 = true},
        // Adjacent literals: 0xE1 must not swallow the following 'A' (0x41).
        {.label = "utf8.invalid_continuation.on",
         .group = Group::ParseKnobbed,
         .code = ErrorCode::Utf8InvalidContinuation,
         .offset = 3,
         .positioned = true,
         .what = "Invalid UTF-8 continuation byte in string at offset 3",
         .input = "\"a\xE1" "A\"",
         .strict_utf8 = true},
        {.label = "utf8.surrogate.on",
         .group = Group::ParseKnobbed,
         .code = ErrorCode::Utf8Surrogate,
         .offset = 2,
         .positioned = true,
         .what = "UTF-8 surrogate codepoint in string at offset 2",
         .input = "\"a\xED\xA0\x80\"",
         .strict_utf8 = true},
        {.label = "utf8.exceeds.on",
         .group = Group::ParseKnobbed,
         .code = ErrorCode::Utf8ExceedsMax,
         .offset = 2,
         .positioned = true,
         .what = "UTF-8 codepoint exceeds U+10FFFF in string at offset 2",
         .input = "\"a\xF4\x90\x80\x80\"",
         .strict_utf8 = true},
        {.label = "utf8.truncated.on",
         .group = Group::ParseKnobbed,
         .code = ErrorCode::Utf8Truncated,
         .offset = 2,
         .positioned = true,
         .what = "Truncated UTF-8 sequence in string at offset 2",
         .input = "\"a\xE1\"",
         .strict_utf8 = true},
        {.label = "utf8.overlong_c0.off",
         .group = Group::ParseKnobbed,
         .code = ErrorCode::None,
         .what = "",
         .input = "\"a\xC0\x80\""},
        {.label = "utf8.invalid_lead_ff.off",
         .group = Group::ParseKnobbed,
         .code = ErrorCode::None,
         .what = "",
         .input = "\"a\xFF\""},
        {.label = "utf8.surrogate.off",
         .group = Group::ParseKnobbed,
         .code = ErrorCode::None,
         .what = "",
         .input = "\"a\xED\xA0\x80\""},

        // ---- C. context-free parse entries (bespoke drivers) -------------
        {.label = "parser.requires_padding",
         .group = Group::ContextFree,
         .code = ErrorCode::ParserRequiresPadding,
         .offset = 0,
         .what = "Parser requires NUL padding (kPaddingWidth trailing NUL bytes); use the parse_* entry points",
         .has_result = false},
        {.label = "insitu.buffer_too_small",
         .group = Group::ContextFree,
         .code = ErrorCode::BufferTooSmall,
         .offset = 0,
         .what = "Buffer too small for in-situ parse"},
        {.label = "insitu.padding_not_nul",
         .group = Group::ContextFree,
         .code = ErrorCode::InSituPaddingNotNul,
         .offset = 0,
         .what = "In-situ buffer padding must be NUL bytes"},
        {.label = "file.open_failed",
         .group = Group::ContextFree,
         .code = ErrorCode::FileOpenFailed,
         .offset = 0,
         .what = "Failed to open file: this_file_absolutely_does_not_exist_999.json",
         .input = "this_file_absolutely_does_not_exist_999.json",
         .detail = "this_file_absolutely_does_not_exist_999.json"},
        {.label = "stream.read_failed",
         .group = Group::ContextFree,
         .code = ErrorCode::StreamReadFailed,
         .offset = 0,
         .what = "Failed to read stream"},
        {.label = "parse_view.truncated",
         .group = Group::ContextFree,
         .code = ErrorCode::UnterminatedString,
         .offset = 4,
         .positioned = true,
         .what = "Unterminated string at offset 4",
         .input = "\"abc"},

        // ---- D. jsonl (line-relative offsets) ----------------------------
        {.label = "jsonl.error_line2",
         .group = Group::Jsonl,
         .code = ErrorCode::ExpectedStringKey,
         .offset = 1,
         .positioned = true,
         .what = "Expected string key in object at offset 1",
         .input = "{\"a\":1}\n{bad}\n"},
        {.label = "jsonl.error_line3_depth",
         .group = Group::Jsonl,
         .code = ErrorCode::MaxDepthExceeded,
         .offset = 1,
         .positioned = true,
         .what = "Maximum nesting depth exceeded at offset 1",
         .input = "[]\n[]\n[[]]\n",
         .max_depth = 1},
        {.label = "jsonl.utf8_line2",
         .group = Group::Jsonl,
         .code = ErrorCode::Utf8InvalidLead,
         .offset = 1,
         .positioned = true,
         .what = "Invalid UTF-8 lead byte in string at offset 1",
         .input = "[]\n\"\xFF\"\n",
         .strict_utf8 = true},

        // ---- E. writer (Category::Json, no offset) -----------------------
        {.label = "writer.non_finite_double",
         .group = Group::Writer,
         .code = ErrorCode::NonFiniteDouble,
         .parse_error = false,
         .category = Category::Json,
         .what = "Cannot serialize non-finite double (NaN/Inf) to JSON"},
        {.label = "writer.dump_max_depth",
         .group = Group::Writer,
         .code = ErrorCode::DumpMaxDepthExceeded,
         .parse_error = false,
         .category = Category::Json,
         .what = "Maximum nesting depth exceeded during dump"},
        {.label = "writer.ascii_invalid_lead",
         .group = Group::Writer,
         .code = ErrorCode::Utf8InvalidLead,
         .parse_error = false,
         .category = Category::Json,
         .what = "Invalid UTF-8 lead byte in string"},
        {.label = "writer.ascii_truncated",
         .group = Group::Writer,
         .code = ErrorCode::Utf8Truncated,
         .parse_error = false,
         .category = Category::Json,
         .what = "Truncated UTF-8 sequence in string"},
        {.label = "writer.ascii_bad_continuation",
         .group = Group::Writer,
         .code = ErrorCode::Utf8InvalidContinuation,
         .parse_error = false,
         .category = Category::Json,
         .what = "Invalid UTF-8 continuation byte in string"},
        {.label = "writer.ascii_overlong",
         .group = Group::Writer,
         .code = ErrorCode::Utf8Overlong,
         .parse_error = false,
         .category = Category::Json,
         .what = "Overlong UTF-8 sequence in string"},
        {.label = "writer.ascii_exceeds",
         .group = Group::Writer,
         .code = ErrorCode::Utf8ExceedsMax,
         .parse_error = false,
         .category = Category::Json,
         .what = "UTF-8 codepoint exceeds U+10FFFF in string"},
        {.label = "writer.ascii_surrogate",
         .group = Group::Writer,
         .code = ErrorCode::Utf8Surrogate,
         .parse_error = false,
         .category = Category::Json,
         .what = "UTF-8 surrogate codepoint in string"},
        {.label = "writer.file_open_failed",
         .group = Group::Writer,
         .code = ErrorCode::FileWriteOpenFailed,
         .parse_error = false,
         .category = Category::Json,
         .what = "Failed to open file for writing: pjh_no_such_dir_xyz/out.json",
         .input = "pjh_no_such_dir_xyz/out.json",
         .detail = "pjh_no_such_dir_xyz/out.json"},
        {.label = "writer.stream_write_failed",
         .group = Group::Writer,
         .code = ErrorCode::StreamWriteFailed,
         .parse_error = false,
         .category = Category::Json,
         .what = "Failed to write to stream",
         .has_result = false},
        {.label = "dump_jsonl_result.nan",
         .group = Group::Writer,
         .code = ErrorCode::NonFiniteDouble,
         .parse_error = false,
         .category = Category::Json,
         .what = "Cannot serialize non-finite double (NaN/Inf) to JSON"},
        {.label = "dump_jsonl_file_result.missing_dir",
         .group = Group::Writer,
         .code = ErrorCode::FileWriteOpenFailed,
         .parse_error = false,
         .category = Category::Json,
         .what = "Failed to open file for writing: pjh_no_such_dir_xyz/t79.jsonl",
         .input = "pjh_no_such_dir_xyz/t79.jsonl",
         .detail = "pjh_no_such_dir_xyz/t79.jsonl"},
    };

    constexpr size_t kRowCount = sizeof(kAllRows) / sizeof(kAllRows[0]);
    static_assert(kRowCount == 62, "62 deterministic golden rows (the optional "
                                   "POSIX directory row is checked separately)");

    const golden_row *find_row(std::string_view label)
    {
        for (const golden_row &r : kAllRows)
            if (label == r.label)
                return &r;
        return nullptr;
    }

    // -----------------------------------------------------------------------
    // Shared verification: golden binding + throwing channel + Result channel
    // + cross-channel equality.
    // -----------------------------------------------------------------------
    void verify_row(const golden_row &r, const observed &thr, const observed &res,
                    bool has_result)
    {
        CAPTURE(r.label);
        const bool expect_ok = (r.code == ErrorCode::None);

        // 0. Table self-consistency: the static message table plus the row's
        //    {category, offset, positioned, detail} must render the archived
        //    bytes. A message or offset drift reddens right here (mutation
        //    self-check M1/M2).
        if (!expect_ok)
            CHECK(std::string_view(Error{r.code, r.category, r.offset, r.positioned,
                                         r.detail}
                                       .format()) == r.what);

        // 1. Throwing channel vs golden.
        REQUIRE(thr.threw == !expect_ok);
        if (!expect_ok)
        {
            CHECK(thr.is_parse_error == r.parse_error);
            CHECK(thr.category == r.category);
            if (r.parse_error)
            {
                CHECK(thr.has_offset);
                CHECK(thr.offset == r.offset);
            }
            else
            {
                CHECK_FALSE(thr.is_parse_error);
                CHECK_FALSE(thr.has_offset);
            }
            CHECK(thr.what == r.what);
        }
        else
        {
            CHECK_FALSE(thr.is_parse_error);
            CHECK(thr.what.empty());
        }

        // 2. Result channel vs golden + cross-channel equality (guards a
        //    future per-shell post-processing split).
        if (has_result)
        {
            REQUIRE(res.threw == !expect_ok);
            if (!expect_ok)
            {
                CHECK(res.is_parse_error == r.parse_error);
                CHECK(res.category == r.category);
                if (r.parse_error)
                {
                    CHECK(res.has_offset);
                    CHECK(res.offset == r.offset);
                }
                else
                {
                    CHECK_FALSE(res.is_parse_error);
                    CHECK_FALSE(res.has_offset);
                }
                CHECK(res.what == r.what);
                CHECK(same_tuple(thr, res));
            }
            else
            {
                CHECK(res.what.empty());
            }
        }
    }

    // doctest has no per-case teardown: restore every knob this TU touches,
    // even when a REQUIRE unwinds the case.
    struct GoldenConfigGuard
    {
        bool   m_strict_dup;
        bool   m_strip_bom;
        bool   m_strict_utf8;
        size_t m_max_depth;

        GoldenConfigGuard()
            : m_strict_dup(Config::instance().strict_duplicate_keys()),
              m_strip_bom(Config::instance().strip_bom()),
              m_strict_utf8(Config::instance().strict_utf8()),
              m_max_depth(Config::instance().max_depth())
        {
        }

        ~GoldenConfigGuard()
        {
            Config::instance().set_strict_duplicate_keys(m_strict_dup);
            Config::instance().set_strip_bom(m_strip_bom);
            Config::instance().set_strict_utf8(m_strict_utf8);
            Config::instance().set_max_depth(m_max_depth);
        }
    };

    void set_parse_defaults()
    {
        Config::instance().set_strict_duplicate_keys(false);
        Config::instance().set_strip_bom(false);
        Config::instance().set_strict_utf8(false);
        Config::instance().set_max_depth(Config::kDefaultMaxDepth);
    }
} // namespace

// ---------------------------------------------------------------------------
// A. parse_copy, default knobs
// ---------------------------------------------------------------------------
TEST_CASE("Differential errors: parse_copy default knobs, both channels")
{
    GoldenConfigGuard guard;
    for (const golden_row &r : kAllRows)
    {
        if (r.group != Group::ParseDefault)
            continue;
        set_parse_defaults();
        auto thr = observe_throw([&] { (void)parse_copy(r.input); });
        auto res = observe_result(parse_copy_result(r.input));
        verify_row(r, thr, res, true);
    }
}

// ---------------------------------------------------------------------------
// B. parse_copy with Config knobs (BOM / duplicate key / depth / strict UTF-8)
// ---------------------------------------------------------------------------
TEST_CASE("Differential errors: parse_copy with config knobs, both channels")
{
    GoldenConfigGuard guard;
    for (const golden_row &r : kAllRows)
    {
        if (r.group != Group::ParseKnobbed)
            continue;
        Config::instance().set_strict_duplicate_keys(r.strict_dup);
        Config::instance().set_strip_bom(r.strip_bom);
        Config::instance().set_strict_utf8(r.strict_utf8);
        Config::instance().set_max_depth(r.max_depth);
        auto thr = observe_throw([&] { (void)parse_copy(r.input); });
        auto res = observe_result(parse_copy_result(r.input));
        verify_row(r, thr, res, true);
    }
}

// ---------------------------------------------------------------------------
// C. Context-free parse entries (bespoke: distinct entry + resource shapes)
// ---------------------------------------------------------------------------
TEST_CASE("Differential errors: context-free parse entries")
{
    GoldenConfigGuard guard;
    set_parse_defaults();

    // parser.requires_padding: no Result twin; cross-check the kernel slot.
    {
        const golden_row *r = find_row("parser.requires_padding");
        REQUIRE(r != nullptr);
        auto thr = observe_throw(
            []
            {
                Parser p("{}");
                (void)p.parse();
            });
        Parser q("{}");
        Json out;
        REQUIRE_FALSE(q.parse(out));
        const Error &ke = q.error();
        CHECK(ke.code == r->code);
        CHECK(ke.category == r->category);
        CHECK(ke.offset() == r->offset);
        CHECK(std::string_view(ke.format()) == r->what);
        verify_row(*r, thr, observed{}, /*has_result=*/false);
    }

    // insitu.buffer_too_small
    {
        const golden_row *r = find_row("insitu.buffer_too_small");
        REQUIRE(r != nullptr);
        auto thr = observe_throw(
            [] { (void)parse_in_situ(std::pmr::string("{}")); });
        auto res = observe_result(parse_in_situ_result(std::pmr::string("{}")));
        verify_row(*r, thr, res, true);
    }

    // insitu.padding_not_nul
    {
        const golden_row *r = find_row("insitu.padding_not_nul");
        REQUIRE(r != nullptr);
        auto mk = []
        {
            std::pmr::string b;
            b.resize(128, 'x');
            return b;
        };
        auto thr = observe_throw([&] { (void)parse_in_situ(mk()); });
        auto res = observe_result(parse_in_situ_result(mk()));
        verify_row(*r, thr, res, true);
    }

    // file.open_failed: portable relative missing path, synthesized what()
    {
        const golden_row *r = find_row("file.open_failed");
        REQUIRE(r != nullptr);
        auto thr = observe_throw([&] { (void)parse_file(r->input); });
        auto res = observe_result(parse_file_result(r->input));
        verify_row(*r, thr, res, true);
    }

    // stream.read_failed
    {
        const golden_row *r = find_row("stream.read_failed");
        REQUIRE(r != nullptr);
        auto mk = []
        {
            std::istringstream in("{}");
            in.setstate(std::ios::badbit);
            return in;
        };
        auto thr = observe_throw(
            [&]
            {
                auto in = mk();
                (void)parse_from_istream(in);
            });
        auto rin = mk();
        auto res = observe_result(parse_from_istream_result(rin));
        verify_row(*r, thr, res, true);
    }

    // parse_view.truncated: caller-padded view, no copy
    {
        const golden_row *r = find_row("parse_view.truncated");
        REQUIRE(r != nullptr);
        auto mk = []
        {
            std::string b = "\"abc";
            b.resize(b.size() + kPaddingWidth, '\0');
            return b;
        };
        auto thr = observe_throw(
            [&]
            {
                auto b = mk();
                (void)parse_view(b.data(), 4);
            });
        auto res = observe_result(
            [&]
            {
                auto b = mk();
                return parse_view_result(b.data(), 4);
            }());
        verify_row(*r, thr, res, true);
    }

    // Directory path: not a readable file. The is_directory pre-check makes
    // the classification filesystem-independent, so this row is assertable on
    // every platform (including Windows, where ifstream already failed).
    {
        const std::string dir = "pjh_t79_dir_probe";
        std::error_code ec;
        std::filesystem::create_directory(dir, ec);
        const std::string expect = "Failed to open file: " + dir;

        auto thr = observe_throw([&] { (void)parse_file(dir); });
        auto res = observe_result(parse_file_result(dir));

        std::filesystem::remove(dir, ec);

        golden_row r{};
        r.label = "file.directory_read";
        r.group = Group::ContextFree;
        r.code = ErrorCode::FileOpenFailed;
        r.category = Category::Parse;
        r.offset = 0;
        r.positioned = false;
        r.what = expect;
        r.detail = dir;
        r.input = dir;
        verify_row(r, thr, res, true);
    }
}

// ---------------------------------------------------------------------------
// D. jsonl (line-relative offsets)
// ---------------------------------------------------------------------------
TEST_CASE("Differential errors: jsonl line-relative offsets, both channels")
{
    GoldenConfigGuard guard;
    for (const golden_row &r : kAllRows)
    {
        if (r.group != Group::Jsonl)
            continue;
        Config::instance().set_strict_duplicate_keys(false);
        Config::instance().set_strip_bom(false);
        Config::instance().set_strict_utf8(r.strict_utf8);
        Config::instance().set_max_depth(r.max_depth);
        auto thr = observe_throw([&] { (void)parse_jsonl(r.input); });
        auto res = observe_result(parse_jsonl_result(r.input));
        verify_row(r, thr, res, true);
    }
}

// ---------------------------------------------------------------------------
// E. Writer entries (Category::Json, no offset; bespoke typed drivers)
// ---------------------------------------------------------------------------
TEST_CASE("Differential errors: writer entries, both channels")
{
    GoldenConfigGuard guard;
    set_parse_defaults();

#ifndef __FAST_MATH__
    // NaN rows are meaningless under -ffast-math (isfinite may be folded).
    {
        const golden_row *r = find_row("writer.non_finite_double");
        REQUIRE(r != nullptr);
        Json nan{std::numeric_limits<double>::quiet_NaN()};
        auto thr = observe_throw([&] { (void)dump(nan); });
        auto res = observe_result(dump_result(nan));
        verify_row(*r, thr, res, true);
    }
    {
        const golden_row *r = find_row("dump_jsonl_result.nan");
        REQUIRE(r != nullptr);
        Array arr = Array::of(Json(std::numeric_limits<double>::quiet_NaN()));
        auto thr = observe_throw([&] { (void)dump_jsonl(arr); });
        auto res = observe_result(dump_jsonl_result(arr));
        verify_row(*r, thr, res, true);
    }
#endif

    // writer.dump_max_depth: parse at the default bound, then lower it so the
    // dump-side bound (not the parse) trips.
    {
        const golden_row *r = find_row("writer.dump_max_depth");
        REQUIRE(r != nullptr);
        Document d = parse_copy("[[[]]]");
        Config::instance().set_max_depth(1);
        auto thr = observe_throw([&] { (void)dump(d.root()); });
        auto res = observe_result(dump_result(d.root()));
        verify_row(*r, thr, res, true);
        Config::instance().set_max_depth(Config::kDefaultMaxDepth);
    }

    // ascii-mode ill-formed UTF-8: six JsonError sites.
    auto ascii_fail = [&](const char *label, const std::string &bad)
    {
        const golden_row *r = find_row(label);
        REQUIRE(r != nullptr);
        DumpOptions opts;
        opts.ascii = true;
        const Json value{std::string_view(bad)};
        auto thr = observe_throw([&] { (void)dump(value, opts); });
        auto res = observe_result(dump_result(value, opts));
        verify_row(*r, thr, res, true);
    };
    ascii_fail("writer.ascii_invalid_lead", std::string("\xFF"));
    ascii_fail("writer.ascii_truncated", std::string("\xE1"));
    // Adjacent literals: 0xE1 must not swallow the following 'A' (0x41).
    ascii_fail("writer.ascii_bad_continuation", std::string("\xE1" "A" "A"));
    ascii_fail("writer.ascii_overlong", std::string("\xC0\x80"));
    ascii_fail("writer.ascii_exceeds", std::string("\xF4\x90\x80\x80"));
    ascii_fail("writer.ascii_surrogate", std::string("\xED\xA0\x80"));

    // writer.file_open_failed: portable relative missing parent directory.
    {
        const golden_row *r = find_row("writer.file_open_failed");
        REQUIRE(r != nullptr);
        auto thr = observe_throw([&] { (void)dump_file(r->input, Json(nullptr)); });
        auto res = observe_result(dump_file_result(r->input, Json(nullptr)));
        verify_row(*r, thr, res, true);
    }

    // writer.stream_write_failed: dump_to(ostream) has no Result twin.
    {
        const golden_row *r = find_row("writer.stream_write_failed");
        REQUIRE(r != nullptr);
        auto thr = observe_throw(
            []
            {
                std::ostringstream os;
                os.setstate(std::ios::badbit);
                dump_to(os, Json(nullptr));
            });
        verify_row(*r, thr, observed{}, /*has_result=*/false);
    }

    // dump_jsonl_file_result.missing_dir (extension row).
    {
        const golden_row *r = find_row("dump_jsonl_file_result.missing_dir");
        REQUIRE(r != nullptr);
        Array arr = Array::of(Json(1));
        auto thr = observe_throw([&] { dump_jsonl_file(r->input, arr); });
        auto res = observe_result(dump_jsonl_file_result(r->input, arr));
        verify_row(*r, thr, res, true);
    }
}

// ---------------------------------------------------------------------------
// Vocabulary coverage: every reachable ErrorCode has a golden row; the
// defensive sites (no public deterministic trigger) are allowlisted.
// ---------------------------------------------------------------------------
TEST_CASE("Differential errors: every reachable ErrorCode has a golden row")
{
    // census §B: unreachable through the public API without changing code.
    constexpr ErrorCode kDefensive[] = {
        ErrorCode::NumberInvalidFormat, ErrorCode::ExpectedQuote,
        ErrorCode::InputTooLarge,       ErrorCode::InvalidCodepoint,
        ErrorCode::FileSizeFailed,      ErrorCode::FileReadFailed,
        ErrorCode::FormatDoubleFailed,  ErrorCode::FormatIntFailed,
        ErrorCode::FileWriteFailed,     ErrorCode::FileWriteCloseFailed,
    };
    auto covered = [](ErrorCode c)
    {
        for (const golden_row &r : kAllRows)
            if (r.code == c)
                return true;
        return false;
    };
    for (uint16_t i = 1; i <= static_cast<uint16_t>(ErrorCode::StreamWriteFailed); ++i)
    {
        const auto c = static_cast<ErrorCode>(i);
        bool defensive = false;
        for (ErrorCode d : kDefensive)
            if (d == c)
                defensive = true;
        if (defensive)
            continue;
        CAPTURE(static_cast<int>(i));
        CHECK(covered(c));
    }
}

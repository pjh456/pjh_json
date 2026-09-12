// Task 79.1 — zero-allocation error kernel vocabulary self-consistency.
//
// Pins the compile-time properties of pjh::json::Error and the static
// message table against the pre-existing exception shell text. The full
// throw-vs-Result differential/golden table lives in 79.5; this TU is the
// additive 79.1 gate only.
#include <doctest/doctest.h>

#include <pjh_result/diagnostic.hpp>

#include <pjh_json/error.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>

using namespace pjh::json;

// ---------------------------------------------------------------------------
// Compile-time kernel properties (plan 79 §3.3 / §6)
// ---------------------------------------------------------------------------
static_assert(std::is_trivially_copyable_v<Error>);
static_assert(std::is_nothrow_move_constructible_v<Error>);
static_assert(pjh::result::Diagnostic<Error>);

// The table is the single source of truth for the detail-less messages; a
// representative slice is pinned at compile time (the full set is checked
// exhaustively at runtime below).
static_assert(message_of(ErrorCode::ExtraCharactersAfterValue) ==
              "Extra characters after complete JSON value");
static_assert(message_of(ErrorCode::MaxDepthExceeded) ==
              "Maximum nesting depth exceeded");
static_assert(message_of(ErrorCode::NumberOutOfRange) ==
              "Number out of double range");
static_assert(message_of(ErrorCode::UnescapedControl) ==
              "Unescaped control character in string");
static_assert(message_of(ErrorCode::NonFiniteDouble) ==
              "Cannot serialize non-finite double (NaN/Inf) to JSON");
static_assert(message_of(ErrorCode::None).empty());

namespace
{
    struct Row
    {
        ErrorCode code;
        std::string_view msg; // message_of(code); may contain "{}"
        bool positioned;
        Category category;
    };

    // Every kernel code except None, in table order. `msg` mirrors
    // message_of byte for byte (detail-carrying rows carry "{}").
    constexpr Row kRows[] = {
        {ErrorCode::ExtraCharactersAfterValue,
         "Extra characters after complete JSON value", true, Category::Parse},
        {ErrorCode::MaxDepthExceeded,
         "Maximum nesting depth exceeded", true, Category::Parse},
        {ErrorCode::ExpectedCommaOrBracket,
         "Expected ',' or ']' in array", true, Category::Parse},
        {ErrorCode::InvalidLiteral,
         "Invalid literal, expected true/false/null", true, Category::Parse},
        {ErrorCode::InvalidLiteralTrailing,
         "Invalid literal, unexpected characters after true/false/null", true, Category::Parse},
        {ErrorCode::NumberNoIntDigits,
         "Invalid number: no digits after '-'", true, Category::Parse},
        {ErrorCode::NumberLeadingZero,
         "Invalid number: leading zeros are not allowed", true, Category::Parse},
        {ErrorCode::NumberNoFracDigits,
         "Invalid number: no digits after decimal point", true, Category::Parse},
        {ErrorCode::NumberNoExpDigits,
         "Invalid number: no digits in exponent", true, Category::Parse},
        {ErrorCode::NumberOutOfRange,
         "Number out of double range", true, Category::Parse},
        {ErrorCode::NumberInvalidFormat,
         "Invalid number format", true, Category::Parse},
        {ErrorCode::ExpectedStringKey,
         "Expected string key in object", true, Category::Parse},
        {ErrorCode::ExpectedColon,
         "Expected ':' in object", true, Category::Parse},
        {ErrorCode::UnexpectedEndOfObject,
         "Unexpected end of object", true, Category::Parse},
        {ErrorCode::ExpectedCommaOrBrace,
         "Expected ',' or '}' in object", true, Category::Parse},
        {ErrorCode::ExpectedQuote,
         "Expected '\"'", true, Category::Parse},
        {ErrorCode::UnterminatedString,
         "Unterminated string", true, Category::Parse},
        {ErrorCode::UnescapedControl,
         "Unescaped control character in string", true, Category::Parse},
        {ErrorCode::InvalidHexDigit,
         "Invalid hex digit in unicode escape", true, Category::Parse},
        {ErrorCode::InvalidEscapeChar,
         "Invalid escape character", true, Category::Parse},
        {ErrorCode::InvalidSurrogatePair,
         "Invalid surrogate pair", true, Category::Parse},
        {ErrorCode::ExpectedLowSurrogate,
         "Expected low surrogate", true, Category::Parse},
        {ErrorCode::LoneLowSurrogate,
         "Lone low surrogate, expected high surrogate first", true, Category::Parse},
        {ErrorCode::UnexpectedCharacter,
         "Unexpected character", true, Category::Parse},
        {ErrorCode::UnexpectedEndOfInput,
         "Unexpected end of input", true, Category::Parse},
        {ErrorCode::UnexpectedValueCharacter,
         "Unexpected character parsing value", true, Category::Parse},
        {ErrorCode::Utf8Overlong,
         "Overlong UTF-8 sequence in string", true, Category::Parse},
        {ErrorCode::Utf8InvalidLead,
         "Invalid UTF-8 lead byte in string", true, Category::Parse},
        {ErrorCode::Utf8InvalidContinuation,
         "Invalid UTF-8 continuation byte in string", true, Category::Parse},
        {ErrorCode::Utf8Surrogate,
         "UTF-8 surrogate codepoint in string", true, Category::Parse},
        {ErrorCode::Utf8ExceedsMax,
         "UTF-8 codepoint exceeds U+10FFFF in string", true, Category::Parse},
        {ErrorCode::Utf8Truncated,
         "Truncated UTF-8 sequence in string", true, Category::Parse},
        {ErrorCode::InputTooLarge,
         "Input too large to pad", false, Category::Parse},
        {ErrorCode::ParserRequiresPadding,
         "Parser requires NUL padding (kPaddingWidth trailing"
         " NUL bytes); use the parse_* entry points", false, Category::Parse},
        {ErrorCode::BufferTooSmall,
         "Buffer too small for in-situ parse", false, Category::Parse},
        {ErrorCode::InSituPaddingNotNul,
         "In-situ buffer padding must be NUL bytes", false, Category::Parse},
        {ErrorCode::DuplicateKey,
         "Duplicate key \"{}\" in object", false, Category::Parse},
        {ErrorCode::InvalidCodepoint,
         "Invalid unicode codepoint", false, Category::Parse},
        {ErrorCode::FileOpenFailed,
         "Failed to open file: {}", false, Category::Parse},
        {ErrorCode::FileSizeFailed,
         "Failed to get file size: {}", false, Category::Parse},
        {ErrorCode::FileReadFailed,
         "Failed to read file: {}", false, Category::Parse},
        {ErrorCode::StreamReadFailed,
         "Failed to read stream", false, Category::Parse},
        {ErrorCode::NonFiniteDouble,
         "Cannot serialize non-finite double (NaN/Inf) to JSON", false, Category::Json},
        {ErrorCode::FormatDoubleFailed,
         "Failed to format double", false, Category::Json},
        {ErrorCode::FormatIntFailed,
         "Failed to format integer", false, Category::Json},
        {ErrorCode::DumpMaxDepthExceeded,
         "Maximum nesting depth exceeded during dump", false, Category::Json},
        {ErrorCode::FileWriteOpenFailed,
         "Failed to open file for writing: {}", false, Category::Json},
        {ErrorCode::FileWriteFailed,
         "Failed to write file: {}", false, Category::Json},
        {ErrorCode::FileWriteCloseFailed,
         "Failed to close file: {}", false, Category::Json},
        {ErrorCode::StreamWriteFailed,
         "Failed to write to stream", false, Category::Json},
    };

    constexpr size_t kPosition = 7;
    constexpr std::string_view kDetail = "X";

    std::string full_text(std::string_view msg, bool positioned)
    {
        std::string out(msg);
        const auto p = out.find("{}");
        if (p != std::string::npos)
            out.replace(p, 2, kDetail);
        if (positioned)
            out += " at offset " + std::to_string(kPosition);
        return out;
    }
}

TEST_CASE("Error: message table mirrors the exception shell text")
{
    for (const Row &row : kRows)
    {
        CAPTURE(static_cast<int>(row.code));

        // Table self-consistency.
        CHECK(message_of(row.code) == row.msg);

        // Detail-carrying codes must carry the placeholder (plan 79 §3.3).
        const bool has_placeholder = row.msg.find("{}") != std::string_view::npos;
        if (has_placeholder)
            CHECK(message_of(row.code).find("{}") != std::string_view::npos);

        // Error value construction/copy/free functions.
        Error err{row.code, row.category, kPosition, row.positioned,
                  has_placeholder ? kDetail : std::string_view{}};
        CHECK(err.has_error());
        CHECK(err.kind() == row.category);
        CHECK(err.offset() == kPosition);
        CHECK(err.message() == row.msg);

        const std::string expected = full_text(row.msg, row.positioned);
        CHECK(err.format() == expected);

        // The (const Error&) shell constructors build what() from the
        // SAME table, preserving type / Category / offset.
        if (row.category == Category::Parse)
        {
            ParseError exc(err);
            CHECK(std::string_view(exc.what()) == expected);
            CHECK(exc.category() == Category::Parse);
            CHECK(exc.offset() == kPosition);
            CHECK(exc.message() == expected);
            CHECK(exc.kind() == Category::Parse);
        }
        else
        {
            JsonError exc(err);
            CHECK(std::string_view(exc.what()) == expected);
            CHECK(exc.category() == Category::Json);
            CHECK(exc.message() == expected);
            CHECK(exc.kind() == Category::Json);
        }
    }
}

TEST_CASE("Error: None is the empty slot sentinel")
{
    Error err{};
    CHECK_FALSE(err.has_error());
    CHECK(err.code == ErrorCode::None);
    CHECK(err.message().empty());
    CHECK(err.format().empty());
    CHECK(err.offset() == 0);
}

TEST_CASE("Error: producer sets Category, writer reuses UTF-8 keys")
{
    // Same vocabulary key, writer producer: Category::Json, no position.
    Error err{ErrorCode::Utf8InvalidLead, Category::Json, 0, false, {}};
    JsonError exc(err);
    CHECK(exc.category() == Category::Json);
    CHECK(std::string_view(exc.what()) == "Invalid UTF-8 lead byte in string");

    // Same key, parser producer: Category::Parse, positioned.
    Error perr{ErrorCode::Utf8InvalidLead, Category::Parse, 2, true, {}};
    ParseError pexc(perr);
    CHECK(pexc.category() == Category::Parse);
    CHECK(pexc.offset() == 2);
    CHECK(std::string_view(pexc.what()) ==
          "Invalid UTF-8 lead byte in string at offset 2");
}

TEST_CASE("Error: positional suffix only when positioned")
{
    Error positioned{ErrorCode::UnterminatedString, Category::Parse, 4, true, {}};
    Error context_free{ErrorCode::UnterminatedString, Category::Parse, 4, false, {}};
    CHECK(positioned.format() == "Unterminated string at offset 4");
    CHECK(context_free.format() == "Unterminated string");
}

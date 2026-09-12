#ifndef INCLUDE_PJH_JSON_ERROR_HPP
#define INCLUDE_PJH_JSON_ERROR_HPP

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace pjh::json
{

    /**
     * @brief Machine-readable error class (no RTTI needed)
     *
     * One value per concrete exception class: ParseError -> Parse,
     * TypeError -> Type, PatchError -> Patch, SchemaError -> Schema, base
     * JsonError (writer/serialization + write-side file I/O) -> Json.
     *
     * @note Deliberately class-level, not site-level: an Io value is not
     *       expressible per class — file I/O failures are ParseError on the
     *       read side (parse_file) and JsonError on the write side
     *       (dump_file). Splitting them would need per-site tagging at ~9
     *       throw sites; the "Failed to <verb> file" message family stays the
     *       human distinction (what() is an implementation detail either way).
     * @note Schema is appended (not reordered) so existing enumerator values
     *       stay stable; no exhaustive switch over Category exists.
     */
    enum class Category
    {
        Parse,
        Type,
        Json,
        Patch,
        Schema
    };

    /**
     * @brief Zero-allocation kernel error vocabulary (one key per failure site)
     *
     * The parser and writer kernels record failures as a fixed-size Error
     * carrying one of these keys; message_of() is the single source of truth
     * for the human-readable text. This is the key for the kernel slot and
     * the message table only.
     *
     * @note Kernel vocabulary, NOT a stable machine contract: the public
     *       exception classes keep their class-level Category contract
     *       (task 16/53). This enum does not change ParseError::category();
     *       it is deliberately site-level while Category stays class-level.
     * @note Not part of any public Result channel today. The public
     *       *_result entries keep their ParseError / JsonError E types
     *       (plan 79 §3.3/§3.7 Variant A); Error is materialised into an
     *       exception before it leaves the implementation.
     * @note OOM is not in this vocabulary: std::bad_alloc remains an
     *       environment failure and propagates unconverted (plan 79 §3.2).
     */
    enum class ErrorCode : uint16_t
    {
        None = 0, // slot empty sentinel

        // ---- parser, positioned: what() appends " at offset N" ----------
        ExtraCharactersAfterValue,
        MaxDepthExceeded,
        ExpectedCommaOrBracket,
        InvalidLiteral,
        InvalidLiteralTrailing,
        NumberNoIntDigits,
        NumberLeadingZero,
        NumberNoFracDigits,
        NumberNoExpDigits,
        NumberOutOfRange,
        NumberInvalidFormat,
        ExpectedStringKey,
        ExpectedColon,
        UnexpectedEndOfObject,
        ExpectedCommaOrBrace,
        ExpectedQuote,
        UnterminatedString,
        UnescapedControl,
        InvalidHexDigit,
        InvalidEscapeChar,
        InvalidSurrogatePair,
        ExpectedLowSurrogate,
        LoneLowSurrogate,
        UnexpectedCharacter,
        UnexpectedEndOfInput,
        UnexpectedValueCharacter,

        // ---- malformed UTF-8 string content ----------------------------
        // Shared by the parser and the ascii-dump writer: the message is
        // identical, the producing side sets Error::category (plan 79 §3.3).
        Utf8Overlong,
        Utf8InvalidLead,
        Utf8InvalidContinuation,
        Utf8Surrogate,
        Utf8ExceedsMax,
        Utf8Truncated,

        // ---- parser, context-free (offset 0, no position suffix) -------
        InputTooLarge,
        ParserRequiresPadding,
        BufferTooSmall,
        InSituPaddingNotNul,
        DuplicateKey,
        InvalidCodepoint,
        FileOpenFailed,
        FileSizeFailed,
        FileReadFailed,
        StreamReadFailed,

        // ---- writer, context-free (Category::Json) ---------------------
        NonFiniteDouble,
        FormatDoubleFailed,
        FormatIntFailed,
        DumpMaxDepthExceeded,
        FileWriteOpenFailed,
        FileWriteFailed,
        FileWriteCloseFailed,
        StreamWriteFailed,
    };

    /**
     * @brief Static message table for ErrorCode (single source of truth)
     *
     * Reproduces the pre-79.1 exception text byte for byte. Context-free
     * detail-carrying codes carry a `{}` placeholder (recorded by the
     * golden capture, `.w1mer/results/79_golden_errors.md`); Error::format()
     * substitutes the borrowed detail. The parser/writer are not wired to
     * this table yet (stages 79.2/79.3).
     *
     * @param c Kernel error key
     * @return Static message for @p c; empty for ErrorCode::None
     */
    constexpr std::string_view message_of(ErrorCode c) noexcept
    {
        switch (c)
        {
        case ErrorCode::None:
            return "";
        case ErrorCode::ExtraCharactersAfterValue:
            return "Extra characters after complete JSON value";
        case ErrorCode::MaxDepthExceeded:
            return "Maximum nesting depth exceeded";
        case ErrorCode::ExpectedCommaOrBracket:
            return "Expected ',' or ']' in array";
        case ErrorCode::InvalidLiteral:
            return "Invalid literal, expected true/false/null";
        case ErrorCode::InvalidLiteralTrailing:
            return "Invalid literal, unexpected characters after true/false/null";
        case ErrorCode::NumberNoIntDigits:
            return "Invalid number: no digits after '-'";
        case ErrorCode::NumberLeadingZero:
            return "Invalid number: leading zeros are not allowed";
        case ErrorCode::NumberNoFracDigits:
            return "Invalid number: no digits after decimal point";
        case ErrorCode::NumberNoExpDigits:
            return "Invalid number: no digits in exponent";
        case ErrorCode::NumberOutOfRange:
            return "Number out of double range";
        case ErrorCode::NumberInvalidFormat:
            return "Invalid number format";
        case ErrorCode::ExpectedStringKey:
            return "Expected string key in object";
        case ErrorCode::ExpectedColon:
            return "Expected ':' in object";
        case ErrorCode::UnexpectedEndOfObject:
            return "Unexpected end of object";
        case ErrorCode::ExpectedCommaOrBrace:
            return "Expected ',' or '}' in object";
        case ErrorCode::ExpectedQuote:
            return "Expected '\"'";
        case ErrorCode::UnterminatedString:
            return "Unterminated string";
        case ErrorCode::UnescapedControl:
            return "Unescaped control character in string";
        case ErrorCode::InvalidHexDigit:
            return "Invalid hex digit in unicode escape";
        case ErrorCode::InvalidEscapeChar:
            return "Invalid escape character";
        case ErrorCode::InvalidSurrogatePair:
            return "Invalid surrogate pair";
        case ErrorCode::ExpectedLowSurrogate:
            return "Expected low surrogate";
        case ErrorCode::LoneLowSurrogate:
            return "Lone low surrogate, expected high surrogate first";
        case ErrorCode::UnexpectedCharacter:
            return "Unexpected character";
        case ErrorCode::UnexpectedEndOfInput:
            return "Unexpected end of input";
        case ErrorCode::UnexpectedValueCharacter:
            return "Unexpected character parsing value";
        case ErrorCode::Utf8Overlong:
            return "Overlong UTF-8 sequence in string";
        case ErrorCode::Utf8InvalidLead:
            return "Invalid UTF-8 lead byte in string";
        case ErrorCode::Utf8InvalidContinuation:
            return "Invalid UTF-8 continuation byte in string";
        case ErrorCode::Utf8Surrogate:
            return "UTF-8 surrogate codepoint in string";
        case ErrorCode::Utf8ExceedsMax:
            return "UTF-8 codepoint exceeds U+10FFFF in string";
        case ErrorCode::Utf8Truncated:
            return "Truncated UTF-8 sequence in string";
        case ErrorCode::InputTooLarge:
            return "Input too large to pad";
        case ErrorCode::ParserRequiresPadding:
            return "Parser requires NUL padding (kPaddingWidth trailing"
                   " NUL bytes); use the parse_* entry points";
        case ErrorCode::BufferTooSmall:
            return "Buffer too small for in-situ parse";
        case ErrorCode::InSituPaddingNotNul:
            return "In-situ buffer padding must be NUL bytes";
        case ErrorCode::DuplicateKey:
            return "Duplicate key \"{}\" in object";
        case ErrorCode::InvalidCodepoint:
            return "Invalid unicode codepoint";
        case ErrorCode::FileOpenFailed:
            return "Failed to open file: {}";
        case ErrorCode::FileSizeFailed:
            return "Failed to get file size: {}";
        case ErrorCode::FileReadFailed:
            return "Failed to read file: {}";
        case ErrorCode::StreamReadFailed:
            return "Failed to read stream";
        case ErrorCode::NonFiniteDouble:
            return "Cannot serialize non-finite double (NaN/Inf) to JSON";
        case ErrorCode::FormatDoubleFailed:
            return "Failed to format double";
        case ErrorCode::FormatIntFailed:
            return "Failed to format integer";
        case ErrorCode::DumpMaxDepthExceeded:
            return "Maximum nesting depth exceeded during dump";
        case ErrorCode::FileWriteOpenFailed:
            return "Failed to open file for writing: {}";
        case ErrorCode::FileWriteFailed:
            return "Failed to write file: {}";
        case ErrorCode::FileWriteCloseFailed:
            return "Failed to close file: {}";
        case ErrorCode::StreamWriteFailed:
            return "Failed to write to stream";
        }
        return "";
    }

    /**
     * @brief Fixed-size, zero-allocation kernel failure value
     *
     * Trivially copyable and nothrow-move: the parser/writer error slot is
     * an inline member, so recording/propagating a failure allocates
     * nothing. Constructing or copying an Error never allocates; format()
     * is the cold materialisation step that does.
     *
     * @note `detail` is a BORROWED view (a file path or a duplicate key)
     *       into a caller/local buffer. It is valid only until the kernel
     *       result is materialised into ParseError/JsonError (done in the
     *       same call frame by the public entries) and MUST NOT outlive
     *       that. Error is not part of any public Result channel today
     *       (plan 79 §3.7 Variant A/B).
     * @note Structurally satisfies pjh::result::Diagnostic (message() +
     *       kind()); error.hpp deliberately does not include pjh_result,
     *       so the concept is asserted in the test TU instead.
     * @note Kernel vocabulary, not a stable machine contract: the public
     *       exception classes keep their class-level Category contract.
     */
    struct Error
    {
        ErrorCode code = ErrorCode::None;
        Category category = Category::Parse; // set by the producing side
        size_t position = 0;                 // byte offset; 0 when !positioned
        bool positioned = false;             // true => format() appends " at offset N"
        std::string_view detail{};           // borrowed; "{}" substitution

        [[nodiscard]] bool has_error() const noexcept
        {
            return code != ErrorCode::None;
        }
        [[nodiscard]] size_t offset() const noexcept { return position; }
        [[nodiscard]] Category kind() const noexcept { return category; }
        [[nodiscard]] std::string_view message() const noexcept
        {
            return message_of(code);
        }

        /**
         * @brief Build the full what() text (allocates)
         * @return message_of(code) with `{}` replaced by detail (when
         *         present) and " at offset N" appended when positioned
         */
        [[nodiscard]] std::string format() const;
    };

    inline std::string Error::format() const
    {
        std::string out(message_of(code));
        // Substitute whenever the template carries a placeholder, even for an
        // empty detail: a duplicate empty key must render `Duplicate key ""`
        // and not leave the literal `{}` (golden table / text identity).
        const auto p = out.find("{}");
        if (p != std::string::npos)
            out.replace(p, 2, detail); // no placeholder => no-op
        if (positioned)
            out += " at offset " + std::to_string(position);
        return out;
    }

    static_assert(std::is_trivially_copyable_v<Error>);
    static_assert(std::is_nothrow_move_constructible_v<Error>);

    /**
     * @brief Machine tag for a path-typed access failure
     */
    enum class AccessErrorKind
    {
        Missing,       // object key / index not present
        OutOfRange,    // array index >= size
        TypeMismatch,  // node is not the expected JSON kind
        InvalidIndex,  // array hop whose key step is not a valid index
        MalformedPath  // path DSL failed to parse
    };

    /**
     * @brief Field-path-carrying access failure (a value, never thrown)
     *
     * Satisfies pjh::result::Diagnostic structurally (message()/kind()), so a
     * Result carrying it can be rendered by pjh::result::render().
     *
     * @note path is a canonical rendering of the failing prefix ('.' joins a
     *       key, "[n]" an index) for diagnostics only; it is not guaranteed to
     *       round-trip through parse_path when a key contains '.', '[' or ']'.
     * @note hop is the 0-based failing hop, SIZE_MAX when not applicable
     *       (MalformedPath, typed extraction).
     * @note expected/actual are static JSON kind names ("null" / "boolean" /
     *       "integer" / "number" / "string" / "array" / "object"); actual is
     *       empty for Missing/OutOfRange/InvalidIndex.
     * @note No source offset: access runs on a parsed tree whose Json nodes
     *       carry no byte position; ParseError::offset() is the position
     *       channel for parse failures.
     */
    struct AccessError
    {
        AccessErrorKind code;
        std::string path;
        std::string_view expected;
        std::string_view actual;
        size_t hop = SIZE_MAX;
        std::string text;

        AccessError(AccessErrorKind c, std::string p,
                    std::string_view exp = {}, std::string_view act = {},
                    size_t h = SIZE_MAX);

        [[nodiscard]] AccessErrorKind kind() const noexcept { return code; }
        [[nodiscard]] std::string_view message() const noexcept { return text; }
    };

    inline AccessError::AccessError(AccessErrorKind c, std::string p,
                                    std::string_view exp, std::string_view act,
                                    size_t h)
        : code(c), path(std::move(p)), expected(exp), actual(act), hop(h)
    {
        switch (code)
        {
        case AccessErrorKind::Missing:
            text = "missing key: " + path;
            break;
        case AccessErrorKind::OutOfRange:
            text = "index out of range: " + path;
            break;
        case AccessErrorKind::TypeMismatch:
            text = "type mismatch at " + path + ": expected " +
                   std::string(expected) + ", got " + std::string(actual);
            break;
        case AccessErrorKind::InvalidIndex:
            text = "invalid array index: " + path;
            break;
        case AccessErrorKind::MalformedPath:
            text = "malformed path: " + path;
            break;
        }
    }

    static_assert(std::is_nothrow_move_constructible_v<AccessError>);

    /**
     * @brief Machine tag for an RFC 6902 JSON Patch application failure
     */
    enum class PatchErrorKind
    {
        InvalidPatchDocument, // top-level not an array / op not an object / missing member
        UnknownOp,            // "op" is not one of add/remove/replace/move/copy/test
        InvalidPointer,       // RFC 6901 grammar error (bad escape / missing leading '/')
        PathNotFound,         // path or from does not resolve
        TypeMismatch,         // parent node is not a container, or the op cannot apply
        IndexOutOfRange,      // array index >= size (or > size for add)
        InvalidArrayIndex,    // leading zero / overflow / '-' in a non-add position
        TestFailed,           // test value not equal
        MoveIntoDescendant,   // from is a proper prefix of path
        RootOperation         // remove/move-from applied to the root pointer
    };

    /**
     * @brief Stable name of a PatchErrorKind (machine tag rendering)
     * @param k Patch error kind
     * @return Static PascalCase name; empty for an out-of-range value
     */
    [[nodiscard]] constexpr std::string_view patch_error_kind_name(PatchErrorKind k) noexcept
    {
        switch (k)
        {
        case PatchErrorKind::InvalidPatchDocument:
            return "InvalidPatchDocument";
        case PatchErrorKind::UnknownOp:
            return "UnknownOp";
        case PatchErrorKind::InvalidPointer:
            return "InvalidPointer";
        case PatchErrorKind::PathNotFound:
            return "PathNotFound";
        case PatchErrorKind::TypeMismatch:
            return "TypeMismatch";
        case PatchErrorKind::IndexOutOfRange:
            return "IndexOutOfRange";
        case PatchErrorKind::InvalidArrayIndex:
            return "InvalidArrayIndex";
        case PatchErrorKind::TestFailed:
            return "TestFailed";
        case PatchErrorKind::MoveIntoDescendant:
            return "MoveIntoDescendant";
        case PatchErrorKind::RootOperation:
            return "RootOperation";
        }
        return "";
    }

    /**
     * @brief Base exception for all JSON errors
     */
    class JsonError : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;

        /**
         * @brief Materialize a kernel failure as a JSON error
         * @param e Fixed-size kernel Error; its format() becomes what()
         * @note Compatibility materialisation point for the zero-allocation
         *       kernel (task 79). The public exception contract
         *       (type / category / what()) is unchanged; detail is copied
         *       into the owned what() string, so @p e may be transient.
         */
        explicit JsonError(const Error &e) : std::runtime_error(e.format()) {}

        /**
         * @brief Machine-readable class (see Category)
         * @return Category::Json for the base class
         */
        [[nodiscard]] virtual Category category() const noexcept
        {
            return Category::Json;
        }

        /**
         * @brief Human-readable message (pjh::result::Diagnostic protocol)
         * @return View of what(); stable for this object's lifetime
         */
        [[nodiscard]] std::string_view message() const noexcept
        {
            return std::string_view(what());
        }

        /**
         * @brief Stable machine tag (pjh::result::Diagnostic protocol)
         * @return category(): Parse for ParseError, Type for TypeError,
         *         Json for the base class
         */
        [[nodiscard]] Category kind() const noexcept { return category(); }
    };

    /**
     * @brief JSON parse failure
     * @note The what() text is an implementation detail (positioned failures
     *       read "<msg> at offset N" in all build modes). The stable
     *       contract for positioned failures is the exception type +
     *       offset(); use it, not string parsing, for machine consumers.
     */
    class ParseError : public JsonError
    {
    public:
        using JsonError::JsonError;

        /**
         * @brief Construct a positioned parse failure
         * @param msg Human-readable message (implementation detail)
         * @param offset Byte offset from the start of the input
         */
        ParseError(std::string msg, size_t offset)
            : JsonError(std::move(msg)), m_offset(offset)
        {
        }

        /**
         * @brief Materialize a positioned kernel failure
         * @param e Fixed-size kernel Error; e.offset() becomes offset()
         * @note Compatibility materialisation point for the zero-allocation
         *       kernel (task 79). Category stays Category::Parse and the
         *       what() text is built from the same static message table.
         */
        explicit ParseError(const Error &e)
            : JsonError(e.format()), m_offset(e.offset())
        {
        }

        /**
         * @brief Byte offset of the failure from the start of the input
         * @return The offset for positioned failures (throw_parse_error);
         *         0 for context-free failures (file I/O, duplicate keys,
         *         invalid codepoint, padding contract)
         */
        [[nodiscard]] constexpr size_t offset() const noexcept
        {
            return m_offset;
        }

        /**
         * @brief Machine-readable class (see Category)
         * @return Category::Parse
         */
        [[nodiscard]] Category category() const noexcept override
        {
            return Category::Parse;
        }

    private:
        size_t m_offset = 0;
    };

    /**
     * @brief Type mismatch on access
     */
    class TypeError : public JsonError
    {
    public:
        using JsonError::JsonError;

        /**
         * @brief Machine-readable class (see Category)
         * @return Category::Type
         */
        [[nodiscard]] Category category() const noexcept override
        {
            return Category::Type;
        }
    };

    namespace detail
    {
        /**
         * @brief Build the what() text of a PatchError (allocates)
         * @param k Patch error kind
         * @param op Operation name (raw when unknown)
         * @param pointer Failing JSON Pointer (path or from)
         * @param detail Optional extra detail appended as ": <detail>"
         * @return House-style short message fragment
         */
        [[nodiscard]] inline std::string patch_error_message(
            PatchErrorKind k, std::string_view op, std::string_view pointer,
            std::string_view detail)
        {
            std::string msg;
            switch (k)
            {
            case PatchErrorKind::InvalidPatchDocument:
                msg = "invalid patch document";
                break;
            case PatchErrorKind::UnknownOp:
                msg = "unknown patch op: ";
                msg += op;
                break;
            case PatchErrorKind::InvalidPointer:
                msg = "invalid JSON pointer: ";
                msg += pointer;
                break;
            case PatchErrorKind::PathNotFound:
                msg = "path not found: ";
                msg += pointer;
                break;
            case PatchErrorKind::TypeMismatch:
                msg = "type mismatch at ";
                msg += pointer;
                break;
            case PatchErrorKind::IndexOutOfRange:
                msg = "index out of range: ";
                msg += pointer;
                break;
            case PatchErrorKind::InvalidArrayIndex:
                msg = "invalid array index: ";
                msg += pointer;
                break;
            case PatchErrorKind::TestFailed:
                msg = "test failed at ";
                msg += pointer;
                break;
            case PatchErrorKind::MoveIntoDescendant:
                msg = "cannot move into a descendant: ";
                msg += pointer;
                break;
            case PatchErrorKind::RootOperation:
                msg = "cannot remove or move the document root";
                break;
            }
            if (!detail.empty())
            {
                msg += ": ";
                msg += detail;
            }
            return msg;
        }
    }

    /**
     * @brief Structured RFC 6902 patch application failure
     *
     * Carries the operation context a caller needs to locate the failing
     * operation: patch-array index, operation name, JSON Pointer and a
     * machine tag. Satisfies pjh::result::Diagnostic (message()/kind()) via
     * JsonError, so a Result carrying it can be rendered by
     * pjh::result::render().
     *
     * @note Unlike AccessError (a value that is never thrown), PatchError
     *       derives JsonError because it is both thrown by the compatibility
     *       shell and carried as the E of Result<void, PatchError>.
     * @note op() is the raw "op" value for UnknownOp and the canonical name
     *       otherwise; pointer() is the failing "path" (or "from" when the
     *       failure is on the move/copy source).
     */
    class PatchError : public JsonError
    {
    public:
        /**
         * @brief Construct a structured patch failure
         * @param code Machine tag
         * @param op_index 0-based index of the failing operation
         * @param op Operation name ("op" member; raw when unknown)
         * @param pointer Failing JSON Pointer (path or from)
         * @param detail Optional detail appended to what()
         */
        PatchError(PatchErrorKind code, size_t op_index, std::string op,
                   std::string pointer, std::string detail = {})
            : JsonError(detail::patch_error_message(code, op, pointer, detail)),
              m_code(code), m_op_index(op_index), m_op(std::move(op)),
              m_pointer(std::move(pointer))
        {
        }

        /**
         * @brief Machine tag for the failure
         */
        [[nodiscard]] PatchErrorKind code() const noexcept { return m_code; }

        /**
         * @brief 0-based index of the failing operation in the patch array
         */
        [[nodiscard]] size_t op_index() const noexcept { return m_op_index; }

        /**
         * @brief Name of the failing operation ("op" member; raw when unknown)
         */
        [[nodiscard]] std::string_view op() const noexcept { return m_op; }

        /**
         * @brief Failing JSON Pointer (path, or from for a source failure)
         */
        [[nodiscard]] std::string_view pointer() const noexcept { return m_pointer; }

        /**
         * @brief Machine-readable class (see Category)
         * @return Category::Patch
         */
        [[nodiscard]] Category category() const noexcept override
        {
            return Category::Patch;
        }

    private:
        PatchErrorKind m_code;
        size_t m_op_index;
        std::string m_op;
        std::string m_pointer;
    };

    /**
     * @brief Machine tag for a lightweight-schema validation failure
     *
     * The structural core (task 38.1) produces TypeMismatch, MissingRequired
     * and InvalidSchema. The remaining values are reserved for the value
     * constraints of task 38.2 (enum / const / length / numeric /
     * additionalProperties); they are declared here with the rest of the
     * vocabulary so the enum does not change shape between the two subtasks.
     */
    enum class SchemaErrorKind
    {
        TypeMismatch,       // "type": instance kind differs from the declared name
        MissingRequired,    // "required": a listed key is absent
        UnexpectedProperty, // "additionalProperties": false (38.2)
        TooShort,           // minLength / minItems (38.2)
        TooLong,            // maxLength / maxItems (38.2)
        BelowMinimum,       // minimum / exclusiveMinimum (38.2)
        AboveMaximum,       // maximum / exclusiveMaximum (38.2)
        EnumMismatch,       // enum (38.2)
        ConstMismatch,      // const (38.2)
        InvalidSchema       // keyword parameter shape/value is malformed
    };

    /**
     * @brief Stable name of a SchemaErrorKind (machine tag rendering)
     * @param k Schema error kind
     * @return Static PascalCase name; empty for an out-of-range value
     */
    [[nodiscard]] constexpr std::string_view schema_error_kind_name(SchemaErrorKind k) noexcept
    {
        switch (k)
        {
        case SchemaErrorKind::TypeMismatch:
            return "TypeMismatch";
        case SchemaErrorKind::MissingRequired:
            return "MissingRequired";
        case SchemaErrorKind::UnexpectedProperty:
            return "UnexpectedProperty";
        case SchemaErrorKind::TooShort:
            return "TooShort";
        case SchemaErrorKind::TooLong:
            return "TooLong";
        case SchemaErrorKind::BelowMinimum:
            return "BelowMinimum";
        case SchemaErrorKind::AboveMaximum:
            return "AboveMaximum";
        case SchemaErrorKind::EnumMismatch:
            return "EnumMismatch";
        case SchemaErrorKind::ConstMismatch:
            return "ConstMismatch";
        case SchemaErrorKind::InvalidSchema:
            return "InvalidSchema";
        }
        return "";
    }

    namespace detail
    {
        /**
         * @brief Build the what() text of a SchemaError (allocates)
         * @param k Schema error kind
         * @param path Failing path ('.' joins a key, "[n]" an index; empty = root)
         * @param expected Expected kind / shape / constraint text
         * @param actual Actual kind / value text
         * @param keyword Failing schema keyword ("type", "required", ...)
         * @return House-style short message fragment
         * @note The empty root path renders as `<root>`; path text is a
         *       diagnostic rendering only and does not round-trip when a key
         *       contains '.', '[' or ']'.
         */
        [[nodiscard]] inline std::string schema_error_message(SchemaErrorKind k, std::string_view path,
                                                              std::string_view expected, std::string_view actual,
                                                              std::string_view keyword)
        {
            const std::string display = path.empty() ? std::string("<root>") : std::string(path);
            switch (k)
            {
            case SchemaErrorKind::TypeMismatch:
                return "schema type mismatch at " + display + ": expected " + std::string(expected) + ", got " +
                       std::string(actual);
            case SchemaErrorKind::MissingRequired:
                return "missing required property: " + display;
            case SchemaErrorKind::UnexpectedProperty:
                return "unexpected property: " + display;
            case SchemaErrorKind::TooShort:
                return "too short at " + display + ": expected " + std::string(expected) + ", got " +
                       std::string(actual);
            case SchemaErrorKind::TooLong:
                return "too long at " + display + ": expected " + std::string(expected) + ", got " +
                       std::string(actual);
            case SchemaErrorKind::BelowMinimum:
                return "below minimum at " + display + ": expected " + std::string(expected) + ", got " +
                       std::string(actual);
            case SchemaErrorKind::AboveMaximum:
                return "above maximum at " + display + ": expected " + std::string(expected) + ", got " +
                       std::string(actual);
            case SchemaErrorKind::EnumMismatch:
                return "value not in enum at " + display + ": got " + std::string(actual);
            case SchemaErrorKind::ConstMismatch:
                return "value does not match const at " + display + ": got " + std::string(actual);
            case SchemaErrorKind::InvalidSchema:
                return "invalid schema: " + std::string(keyword);
            }
            return {};
        }
    } // namespace detail

    /**
     * @brief Structured lightweight-schema validation failure
     *
     * Carries the machine tag plus the failing location and the expected /
     * actual / keyword context a caller needs to report a schema violation.
     * Satisfies pjh::result::Diagnostic (message()/kind()) via JsonError, so a
     * Result carrying it can be rendered by pjh::result::render().
     *
     * @note Follows the PatchError model: one type is both thrown by the
     *       compatibility shell (schema::validate) and carried as the E of
     *       Result<void, SchemaError>; unlike AccessError it is not a pure
     *       value. See include/pjh_json/schema.hpp.
     * @note All four text fields are owned strings: the message is assembled
     *       once in the constructor and expected/actual may be formatted
     *       text (e.g. "length>=3"), so no borrowed view can dangle.
     */
    class SchemaError : public JsonError
    {
    public:
        /**
         * @brief Construct a structured schema failure
         * @param code Machine tag
         * @param path Failing path (empty = root, rendered as `<root>` in what())
         * @param expected Expected kind / shape / constraint text
         * @param actual Actual kind / value text
         * @param keyword Failing schema keyword ("type", "required", "schema", ...)
         */
        SchemaError(SchemaErrorKind code, std::string path, std::string expected = {}, std::string actual = {},
                    std::string keyword = {})
            : JsonError(detail::schema_error_message(code, path, expected, actual, keyword)),
              m_code(code),
              m_path(std::move(path)),
              m_expected(std::move(expected)),
              m_actual(std::move(actual)),
              m_keyword(std::move(keyword))
        {
        }

        /**
         * @brief Machine tag for the failure
         */
        [[nodiscard]] SchemaErrorKind code() const noexcept { return m_code; }

        /**
         * @brief Failing path ('.' joins a key, "[n]" an index; empty = root)
         * @note Diagnostic rendering only; not guaranteed to round-trip when a
         *       key contains '.', '[' or ']'.
         */
        [[nodiscard]] std::string_view path() const noexcept { return m_path; }

        /**
         * @brief Expected kind / shape / constraint text
         */
        [[nodiscard]] std::string_view expected() const noexcept { return m_expected; }

        /**
         * @brief Actual kind / value text
         */
        [[nodiscard]] std::string_view actual() const noexcept { return m_actual; }

        /**
         * @brief Failing schema keyword ("type", "required", "properties", ...)
         */
        [[nodiscard]] std::string_view keyword() const noexcept { return m_keyword; }

        /**
         * @brief Machine-readable class (see Category)
         * @return Category::Schema
         */
        [[nodiscard]] Category category() const noexcept override { return Category::Schema; }

    private:
        SchemaErrorKind m_code;
        std::string m_path;
        std::string m_expected;
        std::string m_actual;
        std::string m_keyword;
    };
}

#endif

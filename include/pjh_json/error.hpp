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
     * TypeError -> Type, base JsonError (writer/serialization + write-side
     * file I/O) -> Json.
     *
     * @note Deliberately class-level, not site-level: an Io value is not
     *       expressible per class — file I/O failures are ParseError on the
     *       read side (parse_file) and JsonError on the write side
     *       (dump_file). Splitting them would need per-site tagging at ~9
     *       throw sites; the "Failed to <verb> file" message family stays the
     *       human distinction (what() is an implementation detail either way).
     */
    enum class Category { Parse, Type, Json };

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
     * @brief Base exception for all JSON errors
     */
    class JsonError : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;

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

}

#endif

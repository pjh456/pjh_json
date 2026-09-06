#ifndef INCLUDE_PJH_JSON_ERROR_HPP
#define INCLUDE_PJH_JSON_ERROR_HPP

#include <cstddef>
#include <stdexcept>
#include <string>

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

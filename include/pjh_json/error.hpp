#ifndef INCLUDE_PJH_JSON_ERROR_HPP
#define INCLUDE_PJH_JSON_ERROR_HPP

#include <cstddef>
#include <stdexcept>
#include <string>

namespace pjh::json
{

    /**
     * @brief Base exception for all JSON errors
     */
    class JsonError : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
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
    };

}

#endif

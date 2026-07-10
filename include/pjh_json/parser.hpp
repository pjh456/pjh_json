#ifndef INCLUDE_PJH_JSON_PARSER_HPP
#define INCLUDE_PJH_JSON_PARSER_HPP

#include <string_view>
#include <string>
#include <stdexcept>
#include <cstdint>
#include <bit>
#include <charconv>
#include <memory_resource>

#include "document.hpp"

namespace pjh::json
{
    /**
     * @brief Recursive-descent JSON parser (requires padded buffer)
     *
     * Uses SIMD (via xsimd) for whitespace skipping and string scanning.
     * Requires input with 64 trailing NUL bytes for SIMD overread safety.
     * Use parse_copy(), parse_file(), or parse_in_situ() to create correctly
     * padded inputs automatically.
     */
    class Parser
    {
    private:
        const char *m_curr;         // Current parse position
        const char *m_end;          // End of input
        std::pmr::memory_resource *m_resource;  // Allocator for parsed values
        bool m_assume_padded;       // If true, caller guarantees 64 trailing NUL bytes

    public:
        /**
         * @brief Construct parser over a JSON text range
         * @param json Text to parse
         * @param res Allocator for parsed values (default: global config resource)
         * @param assume_padded If true, caller guarantees 64 trailing NUL bytes
         * @note If assume_padded is false, parse() will immediately throw ParseError.
         *       parse_copy/parse_file/parse_in_situ set it to true.
         */
        explicit Parser(
            std::string_view json,
            std::pmr::memory_resource *res = Config::instance().resource(),
            bool assume_padded = false)
            : m_curr(json.data()),
              m_end(json.data() + json.size()),
              m_resource(res),
              m_assume_padded(assume_padded) {}

        /**
         * @brief Parse a complete JSON value
         * @return Fully constructed Json tree
         * @throws ParseError if JSON is invalid or if extra characters follow
         * @note Skips leading and trailing whitespace. Input must be padded
         *       (m_assume_padded must be true).
         */
        [[nodiscard]] Json parse();

        /**
         * @brief Read 4 hex digits at current position (advances cursor)
         * @return Decoded 16-bit value
         * @throws ParseError on non-hex digit
         * @note Called from unicode escape handling during string parsing.
         */
        [[nodiscard]] uint32_t parse_hex4();

    private:
        /**
         * @brief Skip whitespace (SIMD-accelerated via xsimd)
         */
        void skip_whitespace();
        /**
         * @brief Parse any JSON value (returns new Json)
         */
        Json parse_value();
        /**
         * @brief Parse value into existing Json (avoids move)
         */
        void parse_value_inplace(Json &out);
        /**
         * @brief Parse JSON object
         */
        Json parse_object();
        /**
         * @brief Parse JSON array
         */
        Json parse_array();
        /**
         * @brief Parse object into existing Json
         */
        void parse_object_inplace(Json &out);
        /**
         * @brief Parse array into existing Json
         */
        void parse_array_inplace(Json &out);
        /**
         * @brief Parse JSON string (SIMD scan with escape fallback)
         * @return Borrowed string_view into the input buffer
         */
        String parse_string();
        /**
         * @brief Parse JSON number (int64 or double)
         * @return Json holding int64 or double
         */
        Json parse_number();
        /**
         * @brief Parse literal (true/false/null) via bit_cast magic
         * @return Json holding bool or nullptr
         */
        Json parse_literal();
    };
}

#endif // INCLUDE_PJH_JSON_PARSER_HPP

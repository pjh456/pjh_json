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
     * The SIMD scan may read up to one batch (kPaddingWidth NUL bytes) past
     * the content, so the input range must carry that much NUL padding.
     * parse_copy(), parse_file() and parse_jsonl() pad automatically;
     * parse_in_situ() and parse_view() require caller-provided padding
     * (parse_in_situ verifies the tail, parse_view cannot — see
     * kPaddingWidth).
     */
    class Parser
    {
    private:
        const char *m_begin;        // Start of input buffer (for position reporting)
        const char *m_curr;         // Current parse position
        const char *m_end;          // End of input data
        std::pmr::memory_resource *m_resource;  // Allocator for parsed values
        bool m_assume_padded;       // If true, caller guarantees kPaddingWidth trailing NUL bytes
        size_t m_depth = 0;         // Current nesting depth (open containers)
        size_t m_max_depth;         // Captured depth limit (0 = unlimited)

    public:
        /**
         * @brief Construct parser over a JSON text range
         * @param json Text to parse
         * @param res Allocator for parsed values (default: global config resource)
         * @param assume_padded If true, caller guarantees kPaddingWidth
         *                      trailing NUL bytes
         * @note If assume_padded is false, parse() will immediately throw ParseError.
         *       All five parse_* entry points set it to true:
         *       parse_copy/parse_file/parse_jsonl over self-padded buffers,
         *       parse_in_situ/parse_view over caller-padded buffers.
         */
        explicit Parser(
            std::string_view json,
            std::pmr::memory_resource *res = Config::instance().resource(),
            bool assume_padded = false)
            : m_begin(json.data()),
              m_curr(json.data()),
              m_end(json.data() + json.size()),
              m_resource(res),
              m_assume_padded(assume_padded),
              m_max_depth(Config::instance().max_depth()) {}

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
        [[nodiscard]] uint32_t parse_hex4()
        {
            return pjh::json::parse_hex4(m_curr, m_begin);
        }

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
        Json parse_object()
        {
            Json out;
            parse_object_inplace(out);
            return out;
        }

        /**
         * @brief Parse JSON array
         */
        Json parse_array()
        {
            Json out;
            parse_array_inplace(out);
            return out;
        }
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

        /**
         * @brief RAII nesting-depth frame
         *
         * Increments m_depth on construction and decrements on destruction,
         * so every exit path (including exceptions) keeps the counter
         * consistent.
         */
        class DepthFrame
        {
        public:
            explicit DepthFrame(Parser &p) : m_p(&p) { ++m_p->m_depth; }
            ~DepthFrame() { --m_p->m_depth; }
        private:
            Parser *m_p;
        };
    };
}

#endif // INCLUDE_PJH_JSON_PARSER_HPP

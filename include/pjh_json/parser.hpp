#ifndef INCLUDE_PJH_JSON_PARSER_HPP
#define INCLUDE_PJH_JSON_PARSER_HPP

#include <string_view>
#include <string>
#include <stdexcept>
#include <cstdint>
#include <bit>
#include <charconv>
#include <memory_resource>
#include <vector>

#include "document.hpp"

namespace pjh::json
{
    struct Stage1Index;

    /**
     * @brief Recursive-descent JSON parser (requires padded buffer)
     *
     * Uses SIMD (via xsimd) for whitespace skipping and string scanning.
     * Requires input with 64 trailing NUL bytes for SIMD overread safety.
     * Use parse_copy(), parse_file(), or parse_in_situ() to create correctly
     * padded inputs automatically.
     *
     * For large inputs, a two-stage SIMD pipeline can be enabled via
     * Config::set_two_stage_threshold(). Stage 1 builds a structural
     * character index; Stage 2 uses it for O(1) structural navigation.
     */
    class Parser
    {
    private:
        const char *m_curr;         // Current parse position
        const char *m_end;          // End of input data
        const char *m_data;         // Start of input buffer (for two-stage)
        size_t m_data_len;          // Input data length (for two-stage)
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
              m_data(json.data()),
              m_data_len(json.size()),
              m_resource(res),
              m_assume_padded(assume_padded) {}

        /**
         * @brief Parse a complete JSON value
         * @return Fully constructed Json tree
         * @throws ParseError if JSON is invalid or if extra characters follow
         * @note Skips leading and trailing whitespace. Input must be padded
         *       (m_assume_padded must be true). For inputs above the configured
         *       two_stage_threshold, uses SIMD structural index pipeline.
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

        // --- Two-stage parsing (SIMD structural index pipeline) ---

        /**
         * @brief Parse using pre-built structural index
         * @param idx Structural character index from Stage 1
         * @return Fully constructed Json tree
         */
        Json parse_two_stage(const Stage1Index &idx);
        /**
         * @brief Parse object from structural index
         */
        void parse_object_two_stage_inplace(
            Json &out,
            const uint32_t *offsets, const uint8_t *types,
            size_t count, size_t &cursor);
        /**
         * @brief Parse array from structural index
         */
        void parse_array_two_stage_inplace(
            Json &out,
            const uint32_t *offsets, const uint8_t *types,
            size_t count, size_t &cursor);
        /**
         * @brief Parse value from structural index
         */
        void parse_value_two_stage_inplace(
            Json &out,
            const uint32_t *offsets, const uint8_t *types,
            size_t count, size_t &cursor);
    };

    /**
     * @brief Structural character index produced by Stage 1
     *
     * offsets[i] = byte position in the input buffer.
     * types[i]   = the character at that position ('{', '}', '[', ']',
     *              ':', ',', '"') or 0 for escaped quotes.
     */
    struct Stage1Index
    {
        std::pmr::vector<uint32_t> offsets;
        std::pmr::vector<uint8_t> types;
    };

    /**
     * @brief Build structural character index via SIMD scan
     * @param data Input buffer (must have 64-byte NUL padding)
     * @param len  Length of valid data in buffer
     * @param res  Memory resource for index allocation
     * @return Structural index with escaped quotes filtered out
     */
    Stage1Index build_structural_index(
        const char *data, size_t len,
        std::pmr::memory_resource *res);

}

#endif // INCLUDE_PJH_JSON_PARSER_HPP

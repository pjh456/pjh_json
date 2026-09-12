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
#include "error.hpp"

namespace pjh::json
{
    /**
     * @brief Recursive-descent JSON parser (requires padded buffer)
     *
     * Uses SIMD for whitespace skipping and string scanning.
     * The SIMD scan may read up to one batch (kPaddingWidth NUL bytes) past
     * the content, so the input range must carry that much NUL padding.
     * parse_copy(), parse_file(), parse_from_istream() and parse_jsonl() pad
     * automatically;
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
        bool m_strip_bom;           // Strip a leading UTF-8 BOM at parse() start (per-Parser, ctor-captured)
        bool m_strict_utf8;         // Strict UTF-8 validation of string content (per-Parser, ctor-captured)
        size_t m_depth = 0;         // Current nesting depth (open containers)
        size_t m_max_depth;         // Captured depth limit (0 = unlimited, default from Config)
        Error m_error{};            // first failure wins; code==None => ok
        bool m_has_error = false;   // fast early-out for fail()/fail_context()

    public:
        /**
         * @brief Construct parser over a JSON text range
          * @param json Text to parse
          * @param res Allocator for parsed values (default: global config resource)
          * @param assume_padded If true, caller guarantees kPaddingWidth
          *                      trailing NUL bytes
           * @param strip_bom Strip a leading UTF-8 BOM at parse() start.
           *       The single-value entries pass the Config setting;
           *       parse_jsonl's per-line parsers pass false (the BOM is
           *       consumed once at the whole-input start).
           * @param strict_utf8 Validate raw UTF-8 in string content
           *       (Config::strict_utf8); all six parse entries pass it.
           * @note If assume_padded is false, parse() will immediately throw ParseError.
           *       All six parse_* entry points set it to true:
           *       parse_copy/parse_file/parse_from_istream/parse_jsonl over
           *       self-padded buffers,
           *       parse_in_situ/parse_view over caller-padded buffers.
           */
          explicit Parser(
              std::string_view json,
              std::pmr::memory_resource *res = Config::instance().resource(),
              bool assume_padded = false,
              bool strip_bom = false,
              bool strict_utf8 = false)
              : m_begin(json.data()),
                m_curr(json.data()),
                m_end(json.data() + json.size()),
                m_resource(res),
                m_assume_padded(assume_padded),
                m_strip_bom(strip_bom),
                m_strict_utf8(strict_utf8),
                m_max_depth(Config::instance().max_depth()) {}

        /**
         * @brief Zero-throw kernel: parse a complete JSON value into @p out
         * @param out Receives the parsed value on success; untouched on
         *        failure (callers discard it)
         * @return true on success; false when the first failure was recorded
         *         in error()
         * @throws std::bad_alloc only (non-recoverable; plan 79 §3.2)
         * @note Input must be padded (m_assume_padded must be true).
         */
        [[nodiscard]] bool parse(Json &out);

        /**
         * @brief First recorded kernel failure
         * @return The slot; code==None (has_error()==false) when no failure
         * @note `detail` inside the returned Error is a borrowed view, valid
         *       only while the input buffer is alive; the throwing
         *       compatibility shell materialises what() in the same frame.
         */
        [[nodiscard]] const Error &error() const noexcept { return m_error; }

        /**
         * @brief Parse a complete JSON value
         * @return Fully constructed Json tree
         * @throws ParseError if JSON is invalid or if extra characters follow
         * @note Compatibility shell over parse(Json&); behavior is unchanged
         *       (type, Category, offset() and what() are byte-identical).
         * @note Skips leading and trailing whitespace. Input must be padded
         *       (m_assume_padded must be true).
         * @note Optionally strips a leading UTF-8 BOM before
         *       dispatch (ctor flag; m_begin untouched, offsets stay
         *       relative to the original buffer start).
         * @note Optionally validates raw UTF-8 in string content
         *       (ctor flag; first offending byte reported, offsets
         *       relative to the original buffer start).
         */
          [[nodiscard]] Json parse();

        /**
         * @brief Read 4 hex digits at current position (advances cursor)
         * @return Decoded 16-bit value
         * @throws ParseError on non-hex digit
         * @note Compatibility shell over the internal bool form; behavior is
         *       unchanged.
         * @note Called from unicode escape handling during string parsing.
         */
        [[nodiscard]] uint32_t parse_hex4();

    private:
        // ---- error slot helpers (first error wins; zero allocation) ----
        /**
         * @brief Record a positioned failure at @p pos (first error wins)
         * @param c Kernel error key
         * @param pos Offending input position; offset = pos - m_begin
         */
        void fail(ErrorCode c, const char *pos) noexcept
        {
            if (m_has_error)
                return;
            m_error = Error{c, Category::Parse,
                            static_cast<size_t>(pos - m_begin), true, {}};
            m_has_error = true;
        }

        /**
         * @brief Record a context-free failure (first error wins)
         * @param c Kernel error key
         * @param detail Borrowed detail for a `{}`-carrying message
         */
        void fail_context(ErrorCode c, std::string_view detail = {}) noexcept
        {
            if (m_has_error)
                return;
            m_error = Error{c, Category::Parse, 0, false, detail};
            m_has_error = true;
        }

        /**
         * @brief Skip whitespace (SIMD-accelerated)
         */
        void skip_whitespace();
        /**
         * @brief Advance past a leading UTF-8 BOM, if enabled and present
         *
         * No-op unless the ctor flag was set AND bytes 0..2 of the input
         * are EF BB BF. m_begin is untouched: error offsets stay
         * relative to the original buffer start (a value right after
         * the BOM reports offset 3, not 0).
         */
        void skip_leading_bom();
        /**
         * @brief Parse any JSON value into @p out (top-level dispatch)
         * @return false on failure (recorded in m_error)
         * @note Preserves the "Unexpected character parsing value" /
         *       "Unexpected end of input" messages of the top-level dispatch.
         */
        [[nodiscard]] bool parse_value(Json &out);
        /**
         * @brief Parse a value into an existing Json (container-element dispatch)
         * @return false on failure (recorded in m_error)
         * @note Preserves the "Unexpected character" message of the
         *       container-element dispatch (distinct from parse_value()).
         */
        [[nodiscard]] bool parse_value_inplace(Json &out);
        /**
         * @brief Parse object into existing Json
         * @return false on failure (recorded in m_error)
         */
        [[nodiscard]] bool parse_object_inplace(Json &out);
        /**
         * @brief Parse array into existing Json
         * @return false on failure (recorded in m_error)
         */
        [[nodiscard]] bool parse_array_inplace(Json &out);
        /**
         * @brief Parse JSON string (SIMD scan with escape fallback)
         * @param out Receives a borrowed view into the input buffer
         * @return false on failure (recorded in m_error)
         */
        [[nodiscard]] bool parse_string(String &out);
        /**
         * @brief Parse JSON number (int64 or double) into @p out
         * @return false on failure (recorded in m_error)
         * @note The token is malformed, or it is syntactically valid but out
         *       of double range (not representable as a finite double;
         *       RFC 8259 §6 permits this documented range limit).
         * @note The range gate is `std::from_chars`'s errc result; only the
         *       error code is inspected (the output value is unspecified on
         *       out-of-range across standard libraries).
         */
        [[nodiscard]] bool parse_number(Json &out);
        /**
         * @brief Parse literal (true/false/null) into @p out
         * @return false on failure (recorded in m_error)
         */
        [[nodiscard]] bool parse_literal(Json &out);
        /**
         * @brief Read 4 hex digits at the cursor (advances cursor)
         * @param out Receives the decoded 16-bit value
         * @return false on failure (recorded in m_error)
         */
        [[nodiscard]] bool parse_hex4(uint32_t &out);

        /**
         * @brief Byte budget for the initial reserve of an outermost
         *        container's entry vector.
         *
         * The grammatical minimum entry size is only a loose lower bound
         * on the entry count, so a byte budget caps over-reservation for
         * inputs dominated by one huge element (e.g. ["<1 MB string>"]).
         */
        static constexpr size_t kReserveHintBytes = 256 * 1024;

        /**
         * @brief Initial capacity hint for a container's entry vector.
         * @param min_entry_bytes Grammatical minimum bytes per entry
         *        (array "0," = 2; object "\"\":0," = 5).
         * @param entry_bytes Stored element size (sizeof of the entry).
         * @return 4 for nested containers, otherwise the smaller of the
         *         input-derived bound and the byte budget, floored at 4.
         * @note Only the outermost container (m_depth == 1) can bound its
         *       element count from the remaining input. A nested
         *       container's remaining bytes include its parent's tail, so
         *       estimating from them would over-reserve by orders of
         *       magnitude on deep nesting or a long parent tail.
         */
        size_t initial_reserve(size_t min_entry_bytes, size_t entry_bytes) const noexcept
        {
            if (m_depth != 1)
                return 4;
            size_t remaining = static_cast<size_t>(m_end - m_curr);
            size_t by_input = remaining / min_entry_bytes;
            size_t by_budget = kReserveHintBytes / entry_bytes;
            size_t hint = by_input < by_budget ? by_input : by_budget;
            return hint < 4 ? 4 : hint;
        }

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

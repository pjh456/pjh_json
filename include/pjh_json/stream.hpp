#ifndef INCLUDE_PJH_JSON_STREAM_HPP
#define INCLUDE_PJH_JSON_STREAM_HPP

#include <iosfwd>
#include <optional>
#include <string>

#include "config.hpp"
#include "document.hpp"
#include "error.hpp"

namespace pjh::json
{
    /**
     * @brief Incremental reader of newline-delimited JSON (one value per line)
     *
     * Pulls one complete Document per non-blank line from an input stream and
     * keeps only the current line in memory: the footprint is bounded by the
     * longest line plus that line's parsed DOM, independent of the stream
     * size. It reuses parse_copy for each line, so every Config parse
     * knob (max_depth, strict_duplicate_keys, strip_bom, strict_utf8) applies
     * with the same semantics as parse_jsonl.
     *
     * Line semantics match parse_jsonl: '\n' delimits lines, a single trailing
     * '\r' is stripped, and blank/whitespace-only lines are skipped. Error
     * offsets are relative to the failing line. A BOM is consumed once, at the
     * whole-stream start, and only when Config::strip_bom is on; a BOM at the
     * start of any later line is a parse error at that line's offset 0.
     *
     * @note The returned Document owns its own copy of the line, so it stays
     *       valid after the next call and after the reader is destroyed.
     * @note A stream already in a bad state, or a failed read, is reported as
     *       a context-free StreamReadFailed parse error (offset 0).
     */
    class JsonlReader
    {
    public:
        /**
         * @brief Construct over an input stream
         * @param in      Input stream; consumed sequentially (no seek/tellg)
         * @param storage Allocation strategy for each returned Document
         *                (default: global config)
         */
        explicit JsonlReader(std::istream &in,
                             Storage storage = Config::instance().storage());

        /**
         * @brief Copy not allowed (stateful single-pass reader)
         */
        JsonlReader(const JsonlReader &) = delete;
        /**
         * @brief Copy not allowed (stateful single-pass reader)
         */
        JsonlReader &operator=(const JsonlReader &) = delete;

        /**
         * @brief Zero-throw kernel: parse the next non-blank line
         * @param out Receives the parsed Document on success; untouched on
         *            failure
         * @return true = @p out holds a value; false = clean end of stream
         *         (no error) or the first failure (check has_error()/error())
         * @throws std::bad_alloc only (propagated unconverted)
         * @note After a failure every later call returns false without reading
         *       further (sticky), so a loop can drain on the condition alone.
         */
        [[nodiscard]] bool next(Document &out);

        /**
         * @brief true once a failure has been recorded (sticky)
         * @return Whether error() holds a value
         */
        [[nodiscard]] bool has_error() const noexcept;

        /**
         * @brief First recorded parse failure
         * @return The owned ParseError; valid until the reader is destroyed
         * @note Precondition: has_error() is true
         */
        [[nodiscard]] const ParseError &error() const noexcept;

        /**
         * @brief Throwing shell: parse the next non-blank line
         * @return The Document on success, std::nullopt at clean end of stream
         * @throws ParseError on failure (the same value as error())
         * @throws std::bad_alloc only otherwise (propagated unconverted)
         */
        [[nodiscard]] std::optional<Document> next();

    private:
        std::istream &m_in;
        Storage m_storage;
        bool m_started = false;
        std::string m_line;
        std::optional<ParseError> m_error;
    };
}

#endif // INCLUDE_PJH_JSON_STREAM_HPP

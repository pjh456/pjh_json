#ifndef INCLUDE_PJH_JSON_STREAM_HPP
#define INCLUDE_PJH_JSON_STREAM_HPP

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory_resource>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <pjh_result/result.hpp>

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

    /**
     * @brief Incremental JSON event kind (ijson-style)
     *
     * The event stream of one top-level JSON value is:
     * BeginObject ... EndObject / BeginArray ... EndArray for containers,
     * MapKey before each object member value, and one of
     * Null/Boolean/Integer/Double/String per scalar.
     */
    enum class EventType : uint8_t
    {
        BeginObject,
        MapKey,
        EndObject,
        BeginArray,
        EndArray,
        Null,
        Boolean,
        Integer,
        Double,
        String,
    };

    /**
     * @brief One incremental parse event
     *
     * `boolean` / `integer` / `number` carry the scalar payloads; `text`
     * carries decoded content for MapKey and String.
     *
     * @warning `text` is a BORROWED view into the reader's internal token
     *          buffer. It is valid only until the next StreamReader::next()
     *          call or the reader's destruction; the buffer is overwritten by
     *          the following event. Copy it (std::string / Json::own) to
     *          retain it.
     */
    struct JsonEvent
    {
        EventType type = EventType::Null;
        std::string_view text{}; ///< MapKey / String: decoded content
        bool boolean = false;    ///< Boolean payload
        int64_t integer = 0;     ///< Integer payload
        double number = 0.0;     ///< Double payload
    };

    /**
     * @brief How many top-level values a StreamReader consumes
     */
    enum class StreamMode : uint8_t
    {
        SingleRoot, ///< exactly one top-level value; trailing non-whitespace is an error
        MultiValue, ///< zero or more top-level values in sequence (JSONL generalized)
    };

    /**
     * @brief Incremental JSON event reader over an std::istream
     *
     * Pulls one JSON event at a time from a stream of top-level values without
     * ever materialising the whole input or a DOM. An independent,
     * byte-oriented state machine scans a sliding window of `chunk_size`
     * bytes, growing the window (never truncating) so tokens, strings and
     * numbers may straddle refill boundaries. Memory is bounded by O(nesting
     * depth + longest single token + chunk_size): the stream size does not
     * appear.
     *
     * Two modes:
     * - StreamMode::SingleRoot (the default) accepts exactly one top-level
     *   value; after it completes, any further non-whitespace byte is
     *   ExtraCharactersAfterValue.
     * - StreamMode::MultiValue accepts a sequence of top-level values
     *   separated by JSON whitespace or EOF. This is JSON Lines generalized:
     *   newlines (and a single trailing '\r') are ordinary whitespace, blank
     *   lines are skipped, and a value may itself span lines (use JsonlReader
     *   or parse_jsonl for the strict one-value-per-line contract). A stream
     *   with no value is a clean end.
     *
     * This is the explicit divergence from the DOM parser: the event core uses
     * no SIMD and requires NEITHER the kPaddingWidth trailing NUL bytes nor a
     * fully buffered input. Do not feed it through Parser.
     *
     * Shared with the DOM parser: the constexpr grammar (grammar.hpp), the
     * buffered escape decoder (detail/utils.hpp), the strict UTF-8 checker
     * (detail/utf8.hpp) and the Error/ErrorCode kernel vocabulary
     * (error.hpp) -- zero new error codes.
     *
     * @note Config capture-at-construction, matching Parser: max_depth,
     *       strip_bom and strict_utf8 are read once, in the constructor. The
     *       BOM is stripped only at byte 0 of the stream (a later BOM is
     *       rejected like any other unexpected byte), and strict_utf8 gates
     *       the raw bytes inside quoted string content and object keys only.
     *       Config::json5 is deliberately NOT consulted: the event core stays
     *       RFC 8259-only (JSON5 is a separate roadmap item).
     * @note Root completion is observed lazily: in SingleRoot mode the
     *       trailing-content check (ExtraCharactersAfterValue) runs on the
     *       first next() after the event that completes the root, not before
     *       it.
     */
    class StreamReader
    {
    public:
        /**
         * @brief Construct over an input stream in SingleRoot mode
         * @param in         Input stream; consumed sequentially (no seek/tellg)
         * @param chunk_size Refill block size in bytes (default 64 KiB). It is
         *                   also the sliding-window target and is exposed for
         *                   tests that must force tokens across refill
         *                   boundaries. Values below 1 are clamped to 1.
         * @param res        Scratch-buffer resource (default: global config)
         */
        explicit StreamReader(std::istream &in, size_t chunk_size = 64 * 1024,
                              std::pmr::memory_resource *res = Config::instance().resource());

        /**
         * @brief Construct over an input stream with an explicit mode
         * @param in         Input stream; consumed sequentially (no seek/tellg)
         * @param mode       SingleRoot (one value) or MultiValue (a sequence)
         * @param chunk_size Refill block size in bytes (default 64 KiB)
         * @param res        Scratch-buffer resource (default: global config)
         */
        explicit StreamReader(std::istream &in, StreamMode mode, size_t chunk_size = 64 * 1024,
                              std::pmr::memory_resource *res = Config::instance().resource());

        /**
         * @brief Copy not allowed (stateful single-pass reader)
         */
        StreamReader(const StreamReader &) = delete;
        /**
         * @brief Copy not allowed (stateful single-pass reader)
         */
        StreamReader &operator=(const StreamReader &) = delete;

        /**
         * @brief Zero-throw kernel: pull the next event
         * @param out Receives the event on success; untouched on failure
         * @return true = @p out holds an event; false = clean end of the
         *         stream (no error) or the first failure (check
         *         has_error()/error())
         * @throws std::bad_alloc only (propagated unconverted)
         * @note After clean end or failure every later call returns false
         *       immediately (sticky, idempotent).
         * @warning `out.text` (MapKey / String) borrows the reader's token
         *          buffer; see JsonEvent.
         */
        [[nodiscard]] bool next(JsonEvent &out);

        /**
         * @brief true once a failure has been recorded (sticky)
         * @return Whether error() holds a value
         */
        [[nodiscard]] bool has_error() const noexcept;

        /**
         * @brief First recorded kernel failure
         * @return The fixed-size Error slot; `detail` is always empty here, so
         *         the view stays valid until the reader is destroyed
         * @note Precondition: has_error() is true
         */
        [[nodiscard]] const Error &error() const noexcept;

        /**
         * @brief Throwing shell: pull the next event
         * @return The event on success, std::nullopt at clean end
         * @throws ParseError on failure (materialised from error())
         * @throws std::bad_alloc only otherwise (propagated unconverted)
         * @warning Same borrowed-view contract as the kernel.
         */
        [[nodiscard]] std::optional<JsonEvent> next();

        /**
         * @brief Result shell: pull the next event
         * @return Ok(event) on success, Ok(std::nullopt) at clean end,
         *         Err(ParseError) on failure
         * @throws std::bad_alloc only (propagated unconverted)
         * @warning Same borrowed-view contract as the kernel.
         */
        [[nodiscard]] pjh::result::Result<std::optional<JsonEvent>, ParseError> next_result();

    private:
        /**
         * @brief One open container's state
         *
         * Phases encode what the next event/byte must be; the stack depth is
         * the current nesting depth (bounded by max_depth).
         */
        struct Frame
        {
            enum class Kind : uint8_t
            {
                Object,
                Array,
            };
            enum class Phase : uint8_t
            {
                ObjectKeyOrEnd, ///< '{' consumed, or after a member value
                ObjectKey,      ///< after ',' (no trailing comma accepted)
                ObjectColon,    ///< MapKey emitted
                ObjectValue,
                ObjectCommaOrEnd,
                ArrayValueOrEnd, ///< '[' consumed, or after a comma
                ArrayValue,
                ArrayCommaOrEnd,
            };
            Kind kind = Kind::Array;
            Phase phase = Phase::ArrayValueOrEnd;
        };

        // ---- window / refill ----
        /**
         * @brief Compact [m_pos, m_len) to the front and append one chunk
         *
         * Sets m_eof on a short read; sets error() on a hard stream failure.
         */
        void fill();
        /**
         * @brief Grow the unconsumed window to hold at least @p n bytes
         * @return true when at least @p n bytes are available
         */
        [[nodiscard]] bool ensure(size_t n);
        /// @brief Absolute stream offset of the next unconsumed byte
        [[nodiscard]] size_t abs() const noexcept { return m_abs_base + m_pos; }
        /// @brief Consume whitespace, filling until a non-blank byte or EOF
        void skip_ws();
        /**
         * @brief One-time leading-BOM strip at absolute offset 0
         *
         * Mirrors Parser::skip_leading_bom: only when Config::strip_bom was
         * on at construction and only at the very start of the stream. A
         * later BOM is left for the value dispatch to reject.
         */
        void skip_start_bom();

        // ---- token decoders ----
        /// @brief Parse the '"'-delimited string at the cursor into m_token
        [[nodiscard]] bool parse_string_token();
        /// @brief Parse a literal at the cursor (true/false/null)
        [[nodiscard]] bool parse_literal(JsonEvent &out);
        /// @brief Parse and classify a number at the cursor
        [[nodiscard]] bool parse_number(JsonEvent &out);
        /// @brief Expect a value at the cursor and emit its first event
        [[nodiscard]] bool expect_value(JsonEvent &out, bool at_root);
        /// @brief Push a container frame (checks max_depth at open_offset)
        [[nodiscard]] bool enter_container(Frame::Kind kind, size_t open_offset);
        /// @brief Mark the just-emitted value complete in its parent frame
        void complete_value();

        // ---- error slot ----
        /// @brief Record a positioned failure (first error wins)
        void fail(ErrorCode c, size_t offset) noexcept;
        /// @brief Record a context-free failure (first error wins)
        void fail_context(ErrorCode c) noexcept;

        std::istream &m_in;
        std::pmr::vector<char> m_buf;    ///< unconsumed window [m_pos, m_len)
        std::pmr::string m_token;        ///< raw/decoded string token scratch
        std::pmr::vector<Frame> m_stack; ///< open containers (depth)
        StreamMode m_mode;
        size_t m_chunk_size;
        size_t m_max_depth;
        bool m_strip_bom;       ///< Config::strip_bom captured at construction
        bool m_strict_utf8;     ///< Config::strict_utf8 captured at construction
        size_t m_pos = 0;       ///< index of the next unconsumed byte
        size_t m_len = 0;       ///< one past the last valid byte in m_buf
        size_t m_abs_base = 0;  ///< absolute stream offset of m_buf[0]
        size_t m_token_len = 0; ///< decoded length of the current m_token
        bool m_start_checked = false; ///< leading-BOM check has run
        bool m_eof = false;
        bool m_root_done = false;
        bool m_finished = false;
        bool m_has_error = false;
        Error m_error{};
    };
}

#endif // INCLUDE_PJH_JSON_STREAM_HPP

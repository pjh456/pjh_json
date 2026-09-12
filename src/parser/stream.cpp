#include "pjh_json/stream.hpp"
#include "pjh_json/detail/utils.hpp"
#include "pjh_json/detail/utf8.hpp"
#include "pjh_json/grammar.hpp"
#include "pjh_json/json.hpp"

#include <cstring>
#include <istream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace pjh::json
{
    namespace
    {
        // The UTF-8 BOM — the only BOM this library recognizes. Bytes compare
        // as uint8_t: 0xEF is a negative char on two's-complement hosts.
        bool starts_with_utf8_bom(const std::string &s) noexcept
        {
            if (s.size() < 3)
                return false;
            const auto *b = reinterpret_cast<const unsigned char *>(s.data());
            return b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF;
        }

        // Blank/whitespace-only line check, delegating to the shared grammar
        // (RFC 8259: space, tab, CR, LF) exactly like parse_jsonl.
        bool is_blank(const std::string &line) noexcept
        {
            for (char c : line)
            {
                if (!grammar::is_whitespace(static_cast<unsigned char>(c)))
                    return false;
            }
            return true;
        }

        // Byte class Parser::parse_literal accepts after a literal
        // (src/parser/literal.cpp kValidAfterLiteral): whitespace, structural
        // separators, and the NUL sentinel. Absent bytes at stream end stand in
        // for that sentinel, so EOF is always accepted.
        bool valid_after_literal(unsigned char c) noexcept
        {
            switch (c)
            {
            case 0x00:
            case 0x09:
            case 0x0A:
            case 0x0D:
            case 0x20:
            case ',':
            case ':':
            case ']':
            case '}':
                return true;
            default:
                return false;
            }
        }

        // Map a grammar::number_error to the shared ErrorCode vocabulary.
        ErrorCode number_error_code(grammar::number_error e) noexcept
        {
            switch (e)
            {
            case grammar::number_error::no_int_digits:
                return ErrorCode::NumberNoIntDigits;
            case grammar::number_error::leading_zero:
                return ErrorCode::NumberLeadingZero;
            case grammar::number_error::no_frac_digits:
                return ErrorCode::NumberNoFracDigits;
            case grammar::number_error::no_exp_digits:
                return ErrorCode::NumberNoExpDigits;
            case grammar::number_error::ok:
                break;
            }
            return ErrorCode::NumberInvalidFormat;
        }

        // Trailing NUL lookahead appended to the raw string token before the
        // buffered escape decoder runs. handle_escape reads at most 12 bytes
        // from a backslash (a surrogate pair's second \uXXXX), so 16 is safe;
        // the decoder writes at or below its read cursor.
        constexpr size_t kStringDecodePadding = 16;

        // A JSON Pointer token made only of decimal digits indexes an array.
        // Returns false for anything else (including overflow, which is then
        // treated as an object member name).
        bool parse_decimal_index(std::string_view s, size_t &out) noexcept
        {
            if (s.empty())
                return false;
            size_t value = 0;
            constexpr size_t kMax = std::numeric_limits<size_t>::max();
            for (char c : s)
            {
                if (c < '0' || c > '9')
                    return false;
                const size_t digit = static_cast<size_t>(c - '0');
                if (value > (kMax - digit) / 10)
                    return false;
                value = value * 10 + digit;
            }
            out = value;
            return true;
        }

        // Scratch containers must never be built on a null resource; the
        // public default is Config::resource(), but a caller may pass null.
        std::pmr::memory_resource *scratch_resource(std::pmr::memory_resource *res) noexcept
        {
            return res ? res : std::pmr::new_delete_resource();
        }
    }

    JsonlReader::JsonlReader(std::istream &in, Storage storage)
        : m_in(in), m_storage(storage)
    {
    }

    /*
     * Zero-throw kernel: pull the next non-blank line and parse it.
     *
     * 1. Sticky failure: return false immediately once m_error is set.
     * 2. Read one line (getline strips '\n'); strip one trailing '\r'.
     * 3. Consume a whole-stream BOM at most once, on the first physical
     *    line, and only when strip_bom is on. A BOM at the start of any
     *    other line is rejected here: parse_copy would otherwise strip it,
     *    but parse_jsonl's per-line parsers run with strip_bom=false.
     * 4. Skip blank/whitespace-only lines.
     * 5. Delegate to parse_copy_result: line-relative offsets and Config
     *    inheritance (max_depth / strict_duplicate_keys / strict_utf8) come
     *    from the same implementation parse_jsonl uses per line.
     */
    bool JsonlReader::next(Document &out)
    {
        if (m_error.has_value())
            return false;

        for (;;)
        {
            m_line.clear();
            if (!std::getline(m_in, m_line))
            {
                if (m_in.bad())
                {
                    m_error.emplace(Error{ErrorCode::StreamReadFailed,
                                          Category::Parse, 0, false, {}});
                }
                return false;
            }

            if (!m_line.empty() && m_line.back() == '\r')
                m_line.pop_back();

            const bool whole_stream_start = !m_started;
            m_started = true;

            if (whole_stream_start && Config::instance().strip_bom() &&
                starts_with_utf8_bom(m_line))
            {
                m_line.erase(0, 3);
            }
            if (Config::instance().strip_bom() && starts_with_utf8_bom(m_line))
            {
                m_error.emplace(Error{ErrorCode::UnexpectedValueCharacter,
                                      Category::Parse, 0, true, {}});
                return false;
            }

            if (is_blank(m_line))
                continue;

            auto r = parse_copy_result(m_line, m_storage);
            if (r.is_err())
            {
                m_error = std::move(r).unwrap_err();
                return false;
            }
            out = std::move(r).unwrap();
            return true;
        }
    }

    bool JsonlReader::has_error() const noexcept
    {
        return m_error.has_value();
    }

    const ParseError &JsonlReader::error() const noexcept
    {
        return *m_error;
    }

    /*
     * Throwing shell over the kernel. The temporary Document uses
     * Storage::SystemDefault (no arena) so the successful path carries no
     * throwaway arena; the parsed value's own arena arrives via move-assign.
     */
    std::optional<Document> JsonlReader::next()
    {
        Document out(Storage::SystemDefault);
        if (!next(out))
        {
            if (m_error.has_value())
                throw *m_error;
            return std::nullopt;
        }
        return std::optional<Document>(std::move(out));
    }

    // ======================================================================
    // StreamReader — single-root incremental event core
    // ======================================================================

    StreamReader::StreamReader(std::istream &in, size_t chunk_size, std::pmr::memory_resource *res)
        : StreamReader(in, StreamMode::SingleRoot, chunk_size, res)
    {
    }

    StreamReader::StreamReader(std::istream &in, StreamMode mode, size_t chunk_size, std::pmr::memory_resource *res)
        : m_in(in),
          m_buf(scratch_resource(res)),
          m_token(scratch_resource(res)),
          m_stack(scratch_resource(res)),
          m_mode(mode),
          m_chunk_size(chunk_size == 0 ? 1 : chunk_size),
          m_max_depth(Config::instance().max_depth()),
          m_strip_bom(Config::instance().strip_bom()),
          m_strict_utf8(Config::instance().strict_utf8())
    {
        m_buf.reserve(m_chunk_size);
        m_stack.reserve(16);
    }

    /*
     * Compact the unconsumed window to the front, then append one chunk.
     * A short extraction is the normal end of stream; only badbit marks a real
     * read failure (a 0-byte read at EOF may set failbit, which is not an
     * error) — the same discipline parse_from_istream uses.
     */
    void StreamReader::fill()
    {
        if (m_has_error || m_eof)
            return;

        if (m_pos != 0)
        {
            const size_t remaining = m_len - m_pos;
            if (remaining != 0)
                std::memmove(m_buf.data(), m_buf.data() + m_pos, remaining);
            m_abs_base += m_pos;
            m_len = remaining;
            m_pos = 0;
        }

        const size_t need = m_len + m_chunk_size;
        if (m_buf.size() < need)
            m_buf.resize(need);

        m_in.read(m_buf.data() + m_len, static_cast<std::streamsize>(m_chunk_size));
        const std::streamsize got = m_in.gcount();
        if (got > 0)
            m_len += static_cast<size_t>(got);
        if (got < static_cast<std::streamsize>(m_chunk_size))
        {
            if (m_in.bad())
            {
                fail_context(ErrorCode::StreamReadFailed);
                return;
            }
            m_eof = true;
        }
    }

    bool StreamReader::ensure(size_t n)
    {
        while (m_len - m_pos < n && !m_eof && !m_has_error)
            fill();
        return m_len - m_pos >= n;
    }

    void StreamReader::skip_ws()
    {
        while (!m_has_error)
        {
            if (m_len == m_pos)
            {
                if (m_eof)
                    return;
                fill();
                continue;
            }
            if (!grammar::is_whitespace(static_cast<unsigned char>(m_buf[m_pos])))
                return;
            ++m_pos;
        }
    }

    /*
     * Strip the UTF-8 BOM once, at absolute offset 0, when strip_bom was on
     * at construction. Runs before the first value's whitespace skip, so a
     * BOM after leading whitespace is NOT stripped (Parser::skip_leading_bom
     * requires m_curr == m_begin). Absent bytes at stream start leave m_pos
     * untouched: a short stream simply has no BOM.
     */
    void StreamReader::skip_start_bom()
    {
        if (m_start_checked)
            return;
        m_start_checked = true;
        if (!m_strip_bom || m_has_error)
            return;
        if (!ensure(3))
            return; // EOF (or read failure) before three bytes: no BOM
        if (static_cast<unsigned char>(m_buf[m_pos]) == 0xEF && static_cast<unsigned char>(m_buf[m_pos + 1]) == 0xBB &&
            static_cast<unsigned char>(m_buf[m_pos + 2]) == 0xBF)
            m_pos += 3;
    }

    void StreamReader::fail(ErrorCode c, size_t offset) noexcept
    {
        if (m_has_error)
            return;
        m_error = Error{c, Category::Parse, offset, true, {}};
        m_has_error = true;
    }

    void StreamReader::fail_context(ErrorCode c) noexcept
    {
        if (m_has_error)
            return;
        m_error = Error{c, Category::Parse, 0, false, {}};
        m_has_error = true;
    }

    /*
     * Accumulate one raw '"'-delimited string body into m_token across refill
     * boundaries, then decode escapes in place. A token that crosses chunk
     * boundaries is grown in m_token rather than truncated.
     *
     * Error ordering mirrors Parser::parse_string exactly so the two report
     * the same first failure:
     * - No escape: the DOM fast path finds a raw control byte before it runs
     *   the UTF-8 gate (UnescapedControl wins over an earlier ill-formed
     *   sequence), then checks strict UTF-8 over the whole content.
     * - With escapes: DOM phase 2 feeds bytes to the UTF-8 checker in stream
     *   order before handling the escape/control at each byte; the checker
     *   sees the backslash but not the escape's ASCII source bytes.
     */
    bool StreamReader::parse_string_token()
    {
        ++m_pos; // consume the opening quote
        const size_t content_base = abs();
        m_token.clear();
        bool escaped = false;
        bool has_escape = false;
        size_t first_control = SIZE_MAX; // raw index of the first raw control byte

        for (;;)
        {
            if (m_len == m_pos)
            {
                if (m_eof)
                {
                    fail(ErrorCode::UnterminatedString, abs());
                    return false;
                }
                fill();
                if (m_has_error)
                    return false;
                continue;
            }
            const char c = m_buf[m_pos];
            ++m_pos;
            if (escaped)
            {
                m_token.push_back(c);
                escaped = false;
            }
            else if (c == '"')
            {
                break;
            }
            else if (c == '\\')
            {
                m_token.push_back(c);
                escaped = true;
                has_escape = true;
            }
            else
            {
                m_token.push_back(c);
                if (static_cast<unsigned char>(c) < 0x20 && first_control == SIZE_MAX)
                    first_control = m_token.size() - 1;
            }
        }

        const size_t raw_len = m_token.size();
        if (!has_escape)
        {
            // DOM phase 1: a raw control byte is found before the UTF-8 gate.
            if (first_control != SIZE_MAX)
            {
                fail(ErrorCode::UnescapedControl, content_base + first_control);
                return false;
            }
            if (m_strict_utf8)
            {
                const char *ep = nullptr;
                const ErrorCode ec = detail::check_utf8_strict(m_token.data(), raw_len, ep);
                if (ec != ErrorCode::None)
                {
                    fail(ec, content_base + static_cast<size_t>(ep - m_token.data()));
                    return false;
                }
            }
            m_token_len = raw_len;
            return true;
        }

        // DOM phase 2: decode in place (destination <= source, so writes never
        // clobber unread input) and interleave the UTF-8 feed. The NUL
        // lookahead bounds handle_escape's read-ahead.
        m_token.append(kStringDecodePadding, '\0');
        char *dst = m_token.data();
        const char *src = m_token.data();
        const char *const src_end = src + raw_len;
        detail::Utf8Checker ck;
        while (src < src_end)
        {
            if (m_strict_utf8)
            {
                ck.feed(static_cast<uint8_t>(*src), src);
                if (ck.failed())
                {
                    fail(ck.code, content_base + static_cast<size_t>(ck.err - m_token.data()));
                    return false;
                }
            }
            if (*src == '\\')
            {
                const char *ep = nullptr;
                const ErrorCode ec = handle_escape(dst, src, ep);
                if (ec != ErrorCode::None)
                {
                    if (ep != nullptr)
                        fail(ec, content_base + static_cast<size_t>(ep - m_token.data()));
                    else
                        fail_context(ec);
                    return false;
                }
            }
            else if (static_cast<unsigned char>(*src) < 0x20)
            {
                fail(ErrorCode::UnescapedControl, content_base + static_cast<size_t>(src - m_token.data()));
                return false;
            }
            else
            {
                *dst++ = *src++;
            }
        }
        if (m_strict_utf8)
        {
            ck.end();
            if (ck.failed())
            {
                fail(ck.code, content_base + static_cast<size_t>(ck.err - m_token.data()));
                return false;
            }
        }
        m_token_len = static_cast<size_t>(dst - m_token.data());
        return true;
    }

    /*
     * Match true/false/null. ANY mismatch or truncation is InvalidLiteral at
     * the literal start, matching Parser::parse_literal exactly: the DOM
     * parser reads a fixed 4/5-byte word against NUL padding, so a short
     * `tru`/`fals` is a mismatch there, never UnexpectedEndOfInput (the
     * zero-available case is reported by expect_value before this call).
     * The trailing byte gate mirrors Parser::parse_literal so `truex` fails
     * here rather than at the next structural step.
     */
    bool StreamReader::parse_literal(JsonEvent &out)
    {
        const char c = m_buf[m_pos];
        const std::string_view lit = (c == 't') ? grammar::kTrue : (c == 'f') ? grammar::kFalse : grammar::kNull;
        const size_t start_abs = abs();

        if (!ensure(lit.size()))
        {
            if (m_has_error)
                return false;
            fail(ErrorCode::InvalidLiteral, start_abs);
            return false;
        }

        const char *p = m_buf.data() + m_pos;
        if (!grammar::match_literal(p, m_buf.data() + m_len, lit))
        {
            fail(ErrorCode::InvalidLiteral, start_abs);
            return false;
        }
        m_pos += lit.size();

        // EOF stands in for the DOM parser's NUL sentinel, which its trailing
        // table accepts; only a real following byte can violate the gate.
        if (ensure(1))
        {
            if (!valid_after_literal(static_cast<unsigned char>(m_buf[m_pos])))
            {
                fail(ErrorCode::InvalidLiteralTrailing, abs());
                return false;
            }
        }
        else if (m_has_error)
        {
            return false;
        }

        out = JsonEvent{};
        if (c == 'n')
        {
            out.type = EventType::Null;
        }
        else
        {
            out.type = EventType::Boolean;
            out.boolean = (c == 't');
        }
        return true;
    }

    /*
     * Scan and classify a numeric token. grammar::scan_number runs over the
     * current window; when it stops exactly at the window end and the stream
     * has not ended, the window is grown and the scan restarts, so a token
     * straddling refill boundaries is never truncated. classify_number (shared
     * with Parser::parse_number) then yields int64 vs double with the same
     * anchors the DOM parser reports.
     */
    bool StreamReader::parse_number(JsonEvent &out)
    {
        for (;;)
        {
            const char *e = m_buf.data() + m_len;
            const char *p = m_buf.data() + m_pos;
            grammar::number_scan scan;
            const grammar::number_error ne = grammar::scan_number(p, e, scan);

            if (p == e && !m_eof)
            {
                fill();
                if (m_has_error)
                    return false;
                continue;
            }

            if (ne != grammar::number_error::ok)
            {
                fail(number_error_code(ne), m_abs_base + static_cast<size_t>(p - m_buf.data()));
                return false;
            }

            Json val;
            const char *err_pos = nullptr;
            const ErrorCode ec = classify_number(m_buf.data() + m_pos, p, scan, val, err_pos);
            if (ec != ErrorCode::None)
            {
                fail(ec, m_abs_base + static_cast<size_t>(err_pos - m_buf.data()));
                return false;
            }

            m_pos = static_cast<size_t>(p - m_buf.data());
            out = JsonEvent{};
            if (val.is_int())
            {
                out.type = EventType::Integer;
                out.integer = val.as_int();
            }
            else
            {
                out.type = EventType::Double;
                out.number = val.as_float();
            }
            return true;
        }
    }

    bool StreamReader::enter_container(Frame::Kind kind, size_t open_offset)
    {
        // Root container counts as level 1, matching Parser's DepthFrame.
        if (m_max_depth != 0 && m_stack.size() + 1 > m_max_depth)
        {
            fail(ErrorCode::MaxDepthExceeded, open_offset);
            return false;
        }
        Frame f;
        f.kind = kind;
        f.phase = (kind == Frame::Kind::Object) ? Frame::Phase::ObjectKeyOrEnd : Frame::Phase::ArrayValueOrEnd;
        m_stack.push_back(f);
        return true;
    }

    void StreamReader::complete_value()
    {
        if (m_stack.empty())
        {
            m_root_done = true;
            return;
        }
        Frame &f = m_stack.back();
        f.phase = (f.kind == Frame::Kind::Object) ? Frame::Phase::ObjectCommaOrEnd : Frame::Phase::ArrayCommaOrEnd;
    }

    /*
     * Expect a value at the cursor and emit its first event: a container open,
     * or a scalar. A container leaves its frame on the stack for the following
     * next() calls; a scalar immediately closes its parent value slot.
     */
    bool StreamReader::expect_value(JsonEvent &out, bool at_root)
    {
        skip_ws();
        if (m_has_error)
            return false;
        if (m_len == m_pos)
        {
            fail(at_root ? ErrorCode::UnexpectedEndOfInput : ErrorCode::UnexpectedCharacter, abs());
            return false;
        }

        switch (m_buf[m_pos])
        {
        case '{':
            if (!enter_container(Frame::Kind::Object, abs()))
                return false;
            ++m_pos;
            out = JsonEvent{};
            out.type = EventType::BeginObject;
            return true;
        case '[':
            if (!enter_container(Frame::Kind::Array, abs()))
                return false;
            ++m_pos;
            out = JsonEvent{};
            out.type = EventType::BeginArray;
            return true;
        case '"':
            if (!parse_string_token())
                return false;
            out = JsonEvent{};
            out.type = EventType::String;
            out.text = std::string_view(m_token.data(), m_token_len);
            complete_value();
            return true;
        case 't':
        case 'f':
        case 'n':
            if (!parse_literal(out))
                return false;
            complete_value();
            return true;
        case '-':
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            if (!parse_number(out))
                return false;
            complete_value();
            return true;
        default:
            fail(at_root ? ErrorCode::UnexpectedValueCharacter : ErrorCode::UnexpectedCharacter, abs());
            return false;
        }
    }

    /*
     * Pull one event. The state is (root-done | stack of open containers with
     * a phase each); every call advances exactly one event. Transitions that
     * do not emit (colon, comma) loop internally.
     *
     * SingleRoot: clean end is reached only after the root value completes and
     * trailing content is checked (ExtraCharactersAfterValue otherwise).
     * MultiValue: after each completed value the stream skips whitespace and
     * either starts the next value or ends cleanly at EOF.
     */
    bool StreamReader::next(JsonEvent &out)
    {
        if (!m_start_checked)
            skip_start_bom();

        for (;;)
        {
            if (m_has_error || m_finished)
                return false;

            if (m_root_done)
            {
                if (m_mode == StreamMode::SingleRoot)
                {
                    skip_ws();
                    if (m_has_error)
                        return false;
                    if (m_len != m_pos)
                    {
                        fail(ErrorCode::ExtraCharactersAfterValue, abs());
                        return false;
                    }
                    m_finished = true;
                    return false;
                }
                // MultiValue: top-level values must be separated by JSON
                // whitespace (or EOF). A value that ends where the next byte
                // is not whitespace is ExtraCharactersAfterValue, exactly as
                // SingleRoot would report it.
                if (!ensure(1))
                {
                    if (m_has_error)
                        return false;
                    m_finished = true; // EOF
                    return false;
                }
                if (!grammar::is_whitespace(static_cast<unsigned char>(m_buf[m_pos])))
                {
                    fail(ErrorCode::ExtraCharactersAfterValue, abs());
                    return false;
                }
                skip_ws();
                if (m_has_error)
                    return false;
                if (m_len == m_pos)
                {
                    m_finished = true;
                    return false;
                }
                m_root_done = false;
                continue;
            }

            if (m_stack.empty())
            {
                // MultiValue: a stream with no value is a clean end.
                if (m_mode == StreamMode::MultiValue)
                {
                    skip_ws();
                    if (m_has_error)
                        return false;
                    if (m_len == m_pos)
                    {
                        m_finished = true;
                        return false;
                    }
                }
                return expect_value(out, true);
            }

            Frame &f = m_stack.back();
            switch (f.phase)
            {
            case Frame::Phase::ObjectKeyOrEnd:
            case Frame::Phase::ObjectKey:
            {
                skip_ws();
                if (m_has_error)
                    return false;
                if (m_len == m_pos)
                {
                    fail(ErrorCode::ExpectedStringKey, abs());
                    return false;
                }
                const char c = m_buf[m_pos];
                if (c == '}' && f.phase == Frame::Phase::ObjectKeyOrEnd)
                {
                    ++m_pos;
                    m_stack.pop_back();
                    out = JsonEvent{};
                    out.type = EventType::EndObject;
                    complete_value();
                    return true;
                }
                if (c != '"')
                {
                    fail(ErrorCode::ExpectedStringKey, abs());
                    return false;
                }
                if (!parse_string_token())
                    return false;
                f.phase = Frame::Phase::ObjectColon;
                out = JsonEvent{};
                out.type = EventType::MapKey;
                out.text = std::string_view(m_token.data(), m_token_len);
                return true;
            }
            case Frame::Phase::ObjectColon:
            {
                skip_ws();
                if (m_has_error)
                    return false;
                if (m_len == m_pos || m_buf[m_pos] != ':')
                {
                    fail(ErrorCode::ExpectedColon, abs());
                    return false;
                }
                ++m_pos;
                f.phase = Frame::Phase::ObjectValue;
                continue;
            }
            case Frame::Phase::ObjectValue:
                return expect_value(out, false);
            case Frame::Phase::ObjectCommaOrEnd:
            {
                skip_ws();
                if (m_has_error)
                    return false;
                if (m_len == m_pos)
                {
                    fail(ErrorCode::UnexpectedEndOfObject, abs());
                    return false;
                }
                const char c = m_buf[m_pos];
                if (c == '}')
                {
                    ++m_pos;
                    m_stack.pop_back();
                    out = JsonEvent{};
                    out.type = EventType::EndObject;
                    complete_value();
                    return true;
                }
                if (c == ',')
                {
                    ++m_pos;
                    f.phase = Frame::Phase::ObjectKey;
                    continue;
                }
                fail(ErrorCode::ExpectedCommaOrBrace, abs());
                return false;
            }
            case Frame::Phase::ArrayValueOrEnd:
            {
                skip_ws();
                if (m_has_error)
                    return false;
                if (m_len == m_pos)
                {
                    fail(ErrorCode::UnexpectedCharacter, abs());
                    return false;
                }
                if (m_buf[m_pos] == ']')
                {
                    ++m_pos;
                    m_stack.pop_back();
                    out = JsonEvent{};
                    out.type = EventType::EndArray;
                    complete_value();
                    return true;
                }
                return expect_value(out, false);
            }
            case Frame::Phase::ArrayValue:
                return expect_value(out, false);
            case Frame::Phase::ArrayCommaOrEnd:
            {
                skip_ws();
                if (m_has_error)
                    return false;
                if (m_len == m_pos)
                {
                    fail(ErrorCode::ExpectedCommaOrBracket, abs());
                    return false;
                }
                const char c = m_buf[m_pos];
                if (c == ']')
                {
                    ++m_pos;
                    m_stack.pop_back();
                    out = JsonEvent{};
                    out.type = EventType::EndArray;
                    complete_value();
                    return true;
                }
                if (c == ',')
                {
                    ++m_pos;
                    f.phase = Frame::Phase::ArrayValue;
                    continue;
                }
                fail(ErrorCode::ExpectedCommaOrBracket, abs());
                return false;
            }
            }
        }
        return false; // unreachable: the loop only exits through return
    }

    /*
     * Owned-payload pull. Reuses the borrowed kernel and copies a MapKey or
     * String text into out.text before the reader's token buffer can be
     * reused. The borrowed view is valid at this point (no further pull has
     * happened), so the copy is safe.
     */
    bool StreamReader::next(OwnedJsonEvent &out)
    {
        JsonEvent ev;
        if (!next(ev))
            return false;
        out.type = ev.type;
        out.boolean = ev.boolean;
        out.integer = ev.integer;
        out.number = ev.number;
        if (ev.type == EventType::MapKey || ev.type == EventType::String)
            out.text.assign(ev.text.data(), ev.text.size());
        else
            out.text.clear();
        return true;
    }

    bool StreamReader::has_error() const noexcept
    {
        return m_has_error;
    }

    const Error &StreamReader::error() const noexcept
    {
        return m_error;
    }

    std::optional<JsonEvent> StreamReader::next()
    {
        JsonEvent ev;
        if (!next(ev))
        {
            if (m_has_error)
                throw ParseError(m_error);
            return std::nullopt;
        }
        return ev;
    }

    pjh::result::Result<std::optional<JsonEvent>, ParseError> StreamReader::next_result()
    {
        using ResultT = pjh::result::Result<std::optional<JsonEvent>, ParseError>;
        JsonEvent ev;
        if (!next(ev))
        {
            if (m_has_error)
                return ResultT::Err(ParseError(m_error));
            return ResultT::Ok(std::nullopt);
        }
        return ResultT::Ok(std::optional<JsonEvent>(ev));
    }

    // ======================================================================
    // StreamItems — JSON Pointer prefix filter over the event cursor
    // ======================================================================

    StreamItems StreamReader::items(std::string_view prefix)
    {
        return StreamItems(*this, prefix, m_buf.get_allocator().resource());
    }

    StreamItems::StreamItems(StreamReader &reader, std::string_view prefix, std::pmr::memory_resource *res)
        : m_reader(&reader),
          m_prefix_raw(scratch_resource(res)),
          m_prefix(scratch_resource(res)),
          m_frames(scratch_resource(res))
    {
        parse_prefix(prefix);
    }

    /*
     * Decode the RFC 6901 pointer into m_prefix. The empty string is the root
     * path (no tokens). Key bytes are unescaped in place into m_prefix_raw and
     * referenced by offset so prefix tokens never own a nested string.
     */
    void StreamItems::parse_prefix(std::string_view prefix)
    {
        if (prefix.empty())
            return;
        if (prefix.front() != '/')
            throw std::invalid_argument(
                "StreamReader::items: prefix must be empty or a JSON Pointer beginning with '/'");

        size_t pos = 1;
        for (;;)
        {
            const size_t slash = prefix.find('/', pos);
            const size_t end = (slash == std::string_view::npos) ? prefix.size() : slash;
            const size_t key_offset = m_prefix_raw.size();

            for (size_t i = pos; i < end; ++i)
            {
                char c = prefix[i];
                if (c == '~')
                {
                    if (i + 1 >= end)
                    {
                        m_prefix_raw.resize(key_offset);
                        throw std::invalid_argument("StreamReader::items: invalid JSON Pointer escape");
                    }
                    const char escaped = prefix[++i];
                    if (escaped == '0')
                        c = '~';
                    else if (escaped == '1')
                        c = '/';
                    else
                    {
                        m_prefix_raw.resize(key_offset);
                        throw std::invalid_argument("StreamReader::items: invalid JSON Pointer escape");
                    }
                }
                m_prefix_raw.push_back(c);
            }

            PrefixToken token;
            const std::string_view decoded(m_prefix_raw.data() + key_offset, m_prefix_raw.size() - key_offset);
            if (decoded == "*")
                token.kind = PrefixToken::Kind::Any;
            else if (parse_decimal_index(decoded, token.index))
                token.kind = PrefixToken::Kind::Index;
            else
            {
                token.kind = PrefixToken::Kind::Key;
                token.key_offset = key_offset;
                token.key_len = decoded.size();
            }
            m_prefix.push_back(token);

            if (slash == std::string_view::npos)
                break;
            pos = slash + 1;
        }
    }

    std::string_view StreamItems::token_key(const PrefixToken &token) const noexcept
    {
        return std::string_view(m_prefix_raw.data() + token.key_offset, token.key_len);
    }

    /*
     * Match length of the next child of the top frame: the number of leading
     * prefix tokens the child's path shares with the prefix, or -1 once it has
     * diverged. At the root (no open frame) zero tokens are consumed yet, so a
     * value whose path is empty always starts at 0.
     */
    long StreamItems::child_match_len() const noexcept
    {
        if (m_frames.empty())
            return 0;
        const PathFrame &parent = m_frames.back();
        const long base = parent.match_len;
        if (base < 0 || static_cast<size_t>(base) >= m_prefix.size())
            return -1;
        const PrefixToken &token = m_prefix[static_cast<size_t>(base)];
        if (parent.is_object)
            return m_pending_child_match; // resolved when the key was seen
        if (token.kind == PrefixToken::Kind::Any)
            return base + 1;
        if (token.kind == PrefixToken::Kind::Index && token.index == parent.next_index)
            return base + 1;
        return -1;
    }

    // Object member names are compared against the prefix while the MapKey
    // event's borrowed text is still valid; only the result (advance or
    // diverge) outlives the call.
    void StreamItems::on_map_key(std::string_view key)
    {
        m_pending_child_match = -1;
        if (m_frames.empty() || !m_frames.back().is_object)
            return;
        const long base = m_frames.back().match_len;
        if (base < 0 || static_cast<size_t>(base) >= m_prefix.size())
            return;
        const PrefixToken &token = m_prefix[static_cast<size_t>(base)];
        if (token.kind != PrefixToken::Kind::Key)
            return;
        if (key == token_key(token))
            m_pending_child_match = base + 1;
    }

    /*
     * Pull reader events until one belongs to a prefix match. The path is
     * tracked with a frame per open container; match state is the number of
     * prefix tokens consumed, and a container whose match reaches the prefix
     * length switches to emitting its whole subtree.
     */
    bool StreamItems::next(JsonEvent &out)
    {
        for (;;)
        {
            JsonEvent ev;
            if (!m_reader->next(ev))
                return false;

            switch (ev.type)
            {
            case EventType::BeginObject:
            case EventType::BeginArray:
            {
                const bool is_object = ev.type == EventType::BeginObject;
                if (m_emitting)
                {
                    m_frames.push_back(PathFrame{is_object, -1, 0});
                    ++m_emit_depth;
                    out = ev;
                    return true;
                }
                const long match = child_match_len();
                m_frames.push_back(PathFrame{is_object, match, 0});
                if (static_cast<size_t>(match) == m_prefix.size())
                {
                    m_emitting = true;
                    m_emit_depth = 1;
                    out = ev;
                    return true;
                }
                break;
            }
            case EventType::EndObject:
            case EventType::EndArray:
            {
                if (!m_frames.empty())
                    m_frames.pop_back();
                if (!m_frames.empty() && !m_frames.back().is_object)
                    ++m_frames.back().next_index;
                if (m_emitting)
                {
                    out = ev;
                    if (--m_emit_depth == 0)
                        m_emitting = false;
                    return true;
                }
                break;
            }
            case EventType::MapKey:
            {
                if (m_emitting)
                {
                    out = ev;
                    return true;
                }
                on_map_key(ev.text);
                break;
            }
            default:
            {
                if (m_emitting)
                {
                    out = ev;
                    return true;
                }
                const long match = child_match_len();
                if (!m_frames.empty() && !m_frames.back().is_object)
                    ++m_frames.back().next_index;
                m_pending_child_match = -1;
                if (static_cast<size_t>(match) == m_prefix.size())
                {
                    out = ev;
                    return true;
                }
                break;
            }
            }
        }
    }

    bool StreamItems::has_error() const noexcept
    {
        return m_reader->has_error();
    }

    const Error &StreamItems::error() const noexcept
    {
        return m_reader->error();
    }

    std::optional<JsonEvent> StreamItems::next()
    {
        JsonEvent ev;
        if (!next(ev))
        {
            if (m_reader->has_error())
                throw ParseError(m_reader->error());
            return std::nullopt;
        }
        return ev;
    }
}

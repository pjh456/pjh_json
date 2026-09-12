#include "pjh_json/stream.hpp"
#include "pjh_json/grammar.hpp"

#include <istream>
#include <string>
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
}

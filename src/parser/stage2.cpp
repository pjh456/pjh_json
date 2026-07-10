#include "pjh_json/parser.hpp"
#include "pjh_json/json.hpp"

namespace pjh::json
{
    static void skip_ws(const char *&p, const char *end)
    {
        while (p < end && static_cast<uint8_t>(*p) <= 0x20)
            ++p;
    }

    /*
     * Parse JSON object in-place using structural index.
     *
     * 1. Consume '{' from index cursor.
     * 2. Loop: read '"' key, parse string, consume ':' separator,
     *    parse value, then check for ',' (continue) or '}' (done).
     * 3. String boundaries and separators are index entries — no
     *    byte-at-a-time scanning needed.
     */
    void Parser::parse_object_two_stage_inplace(
        Json &out,
        const uint32_t *offsets, const uint8_t *types,
        size_t count, size_t &cursor)
    {
        cursor++; // consume '{'
        Object obj(m_resource);
        obj.data().reserve(4);

        while (cursor < count)
        {
            uint8_t t = types[cursor];

            if (t == '}')
            {
                cursor++;
                out = std::move(obj);
                return;
            }

            if (t != '"')
                throw ParseError("Expected string key in object");

            // Parse key string
            m_curr = m_data + offsets[cursor];
            String key = parse_string();
            // Advance cursor past both opening and closing quote markers
            // Skip any escaped-quote markers (type=0) between them
            cursor++; // skip opening quote
            while (cursor < count && types[cursor] != '"')
                cursor++;
            if (cursor < count)
                cursor++; // skip closing quote

            // Expect ':'
            if (cursor >= count || types[cursor] != ':')
                throw ParseError("Expected ':' in object");
            cursor++;

            // Position m_curr at start of value content
            m_curr = m_data + offsets[cursor - 1] + 1;
            skip_ws(m_curr, m_data + m_data_len);

            obj.data().emplace_back(key, Json(nullptr));
            parse_value_two_stage_inplace(obj.data().back().second,
                                          offsets, types, count, cursor);

            if (cursor >= count)
                throw ParseError("Unexpected end of object");
            t = types[cursor];
            if (t == '}')
            {
                cursor++;
                out = std::move(obj);
                return;
            }
            if (t == ',')
                cursor++;
            else
                throw ParseError("Expected ',' or '}' in object");
        }
        throw ParseError("Unexpected end of object");
    }

    /*
     * Parse JSON array in-place using structural index.
     *
     * 1. Consume '[' from index cursor.
     * 2. Loop: parse value, then check for ',' (continue) or ']' (done).
     * 3. Values may be containers (dispatching recursively) or leaf
     *    values parsed via the existing scalar parsers.
     */
    void Parser::parse_array_two_stage_inplace(
        Json &out,
        const uint32_t *offsets, const uint8_t *types,
        size_t count, size_t &cursor)
    {
        cursor++; // consume '['
        Array arr(m_resource);
        arr.data().reserve(4);

        while (cursor < count)
        {
            uint8_t t = types[cursor];

            if (t == ']')
            {
                cursor++;
                out = std::move(arr);
                return;
            }

            m_curr = m_data + offsets[cursor - 1] + 1;
            skip_ws(m_curr, m_data + m_data_len);

            arr.data().emplace_back(nullptr);
            parse_value_two_stage_inplace(arr.data().back(),
                                          offsets, types, count, cursor);

            if (cursor >= count)
                throw ParseError("Unexpected end of array");
            t = types[cursor];
            if (t == ']')
            {
                cursor++;
                out = std::move(arr);
                return;
            }
            if (t == ',')
                cursor++;
            else
                throw ParseError("Expected ',' or ']' in array");
        }
        throw ParseError("Unexpected end of array");
    }

    /*
     * Dispatch value parsing from structural index.
     *
     * 1. Look up the type at current cursor position.
     * 2. '{' / '[' -> recursive container parse.
     * 3. Otherwise -> parse from m_curr (scalar parser: string /
     *    number / literal).
     */
    void Parser::parse_value_two_stage_inplace(
        Json &out,
        const uint32_t *offsets, const uint8_t *types,
        size_t count, size_t &cursor)
    {
        uint8_t t = types[cursor];

        if (t == '{')
        {
            parse_object_two_stage_inplace(out, offsets, types, count, cursor);
            return;
        }
        if (t == '[')
        {
            parse_array_two_stage_inplace(out, offsets, types, count, cursor);
            return;
        }

        // Leaf value: dispatch on first byte at m_curr
        if (*m_curr == '"')
        {
            cursor++; // skip opening quote marker
            out = parse_string();
            // skip to after closing quote
            while (cursor < count && types[cursor] != '"')
                cursor++;
            if (cursor < count)
                cursor++;
            return;
        }
        if (*m_curr == 't' || *m_curr == 'f' || *m_curr == 'n')
        {
            out = parse_literal();
            return;
        }
        if (*m_curr == '-' || (*m_curr >= '0' && *m_curr <= '9'))
        {
            out = parse_number();
            return;
        }

        throw ParseError("Unexpected character parsing value");
    }

    /*
     * Two-stage entry point.
     *
     * 1. Verify padding requirement.
     * 2. Position at first structural character.
     * 3. Parse top-level value via index-driven dispatch.
     * 4. Reject trailing content after the last structural position.
     */
    Json Parser::parse_two_stage(const Stage1Index &idx)
    {
        if (!m_assume_padded)
            throw ParseError("Parser requires 64-byte '\\0' padding");

        if (idx.offsets.empty())
            throw ParseError("Empty input");

        size_t cursor = 0;
        Json result;

        m_curr = m_data + idx.offsets[0];
        parse_value_two_stage_inplace(result,
                                       idx.offsets.data(), idx.types.data(),
                                       idx.offsets.size(), cursor);

        if (cursor < idx.offsets.size())
            throw ParseError("Extra characters after complete JSON value");

        // Verify no trailing content after last structural position
        const char *after = m_data + idx.offsets.back() + 1;
        while (after < m_end && static_cast<uint8_t>(*after) <= 0x20)
            ++after;
        if (after < m_end)
            throw ParseError("Extra characters after complete JSON value");

        return result;
    }
}

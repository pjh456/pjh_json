#include "pjh_json/parser.hpp"
#include "pjh_json/json.hpp"

namespace pjh::json
{
    /*
     * In-place value dispatch (avoids move)
     *
     * 1. Skip leading whitespace.
     * 2. Dispatch by the first character to a type-specific parser:
     *    - '{' '[' : in-place variants write directly into out.
     *    - '"' t/f/n '-' 0-9 : assign via operator= or return-value.
     */
    void Parser::parse_value_inplace(Json &out)
    {
        skip_whitespace();
        switch (*m_curr)
        {
        case '{':
            parse_object_inplace(out);
            return;
        case '[':
            parse_array_inplace(out);
            return;
        case '"':
            out = parse_string();
            return;
        case 't':
        case 'f':
        case 'n':
            out = parse_literal();
            return;
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
            out = parse_number();
            return;
        default:
            throw_parse_error("Unexpected character", m_curr, m_begin);
        }
    }

    /*
     * Value dispatch returning a new Json
     *
     * Same dispatch as in-place, but returns a new Json value each call.
     * Also checks for '\0' sentinel to detect truncated input
     * (only hit when padding is zeroed).
     */
    Json Parser::parse_value()
    {
        skip_whitespace();

        switch (*m_curr)
        {
        case '{':
            return parse_object();
        case '[':
            return parse_array();
        case '"':
            return Json(parse_string());
        case 't':
        case 'f':
        case 'n':
            return parse_literal();
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
            return parse_number();
        case '\0':
            if (m_curr >= m_end)
                throw_parse_error("Unexpected end of input", m_curr, m_begin);
            [[fallthrough]];
        default:
            throw_parse_error("Unexpected character parsing value", m_curr, m_begin);
        }
    }
}

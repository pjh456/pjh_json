#include "pjh_json/parser.hpp"
#include "pjh_json/json.hpp"
#include "pjh_json/detail/utils.hpp"

namespace pjh::json
{
    /*
     * In-place value dispatch (container elements; avoids move)
     *
     * 1. Skip leading whitespace.
     * 2. Dispatch by the first character to a type-specific parser:
     *    - '{' '[' : in-place variants write directly into out.
     *    - '"' t/f/n '-' 0-9 : assign via operator= or move.
     * 3. Any other byte is "Unexpected character" (no "parsing value").
     */
    bool Parser::parse_value_inplace(Json &out)
    {
        skip_whitespace();
        switch (*m_curr)
        {
        case '{':
            return parse_object_inplace(out);
        case '[':
            return parse_array_inplace(out);
        case '"':
        {
            String s;
            if (!parse_string(s))
                return false;
            out = std::move(s);
            return true;
        }
        case 't':
        case 'f':
        case 'n':
            return parse_literal(out);
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
            return parse_number(out);
        default:
            fail(ErrorCode::UnexpectedCharacter, m_curr);
            return false;
        }
    }

    /*
     * Value dispatch for the top level (writes into out)
     *
     * Same dispatch as in-place, but also checks the '\0' sentinel to detect
     * truncated input (only hit when padding is zeroed) and keeps the
     * distinct "Unexpected character parsing value" message.
     */
    bool Parser::parse_value(Json &out)
    {
        skip_whitespace();

        switch (*m_curr)
        {
        case '{':
            return parse_object_inplace(out);
        case '[':
            return parse_array_inplace(out);
        case '"':
        {
            String s;
            if (!parse_string(s))
                return false;
            out = std::move(s);
            return true;
        }
        case 't':
        case 'f':
        case 'n':
            return parse_literal(out);
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
            return parse_number(out);
        case '\0':
            if (m_curr >= m_end)
            {
                fail(ErrorCode::UnexpectedEndOfInput, m_curr);
                return false;
            }
            [[fallthrough]];
        default:
            fail(ErrorCode::UnexpectedValueCharacter, m_curr);
            return false;
        }
    }
}

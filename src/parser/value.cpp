#include "pjh_json/parser.hpp"
#include "pjh_json/json.hpp"

namespace pjh::json
{
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
            error("Unexpected character");
        }
    }

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
                error("Unexpected end of input");
            [[fallthrough]];
        default:
            error("Unexpected character parsing value");
        }
    }
}

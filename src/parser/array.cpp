#include "pjh_json/parser.hpp"
#include "pjh_json/json.hpp"

namespace pjh::json
{
    Json Parser::parse_array()
    {
        ++m_curr;
        Array arr(m_resource);
        skip_whitespace();
        if (m_curr < m_end && *m_curr == ']')
        {
            ++m_curr;
            return Json(std::move(arr));
        }

        arr.data().reserve(4);

        while (true)
        {
            arr.data().emplace_back(nullptr);
            parse_value_inplace(arr.data().back());

            skip_whitespace();
            if (*m_curr == ']')
            {
                ++m_curr;
                return Json(std::move(arr));
            }
            if (*m_curr == ',')
                ++m_curr;
            else
                error("Expected ',' or ']' in array");
        }
    }

    void Parser::parse_array_inplace(Json &out)
    {
        ++m_curr;
        Array arr(m_resource);
        skip_whitespace();
        if (m_curr < m_end && *m_curr == ']')
        {
            ++m_curr;
            out = std::move(arr);
            return;
        }

        arr.data().reserve(4);

        while (true)
        {
            arr.data().emplace_back(nullptr);
            parse_value_inplace(arr.data().back());

            skip_whitespace();
            if (*m_curr == ']')
            {
                ++m_curr;
                out = std::move(arr);
                return;
            }
            if (*m_curr == ',')
                ++m_curr;
            else
                error("Expected ',' or ']' in array");
        }
    }
}

#include "pjh_json/parser.hpp"
#include "pjh_json/json.hpp"
#include <unordered_set>

namespace pjh::json
{
    static void check_duplicate_key(
        std::string_view key,
        std::pmr::unordered_set<std::string_view> &seen)
    {
        if (!seen.insert(key).second)
            throw std::runtime_error("JSON Parse Error: Duplicate key in object");
    }

    Json Parser::parse_object()
    {
        ++m_curr;
        Object obj(m_resource);
        skip_whitespace();
        if (*m_curr == '}')
        {
            ++m_curr;
            return Json(std::move(obj));
        }

        obj.data().reserve(4);
        std::pmr::unordered_set<std::string_view> seen(m_resource);
        seen.reserve(8);

        while (true)
        {
            skip_whitespace();
            if (*m_curr != '"')
                error("Expected string key in object");
            auto key = parse_string();
            check_duplicate_key(key, seen);

            skip_whitespace();
            if (*m_curr != ':')
                error("Expected ':' in object");
            ++m_curr;

            obj.data().emplace_back(key, Json(nullptr));
            parse_value_inplace(obj.data().back().second);

            skip_whitespace();
            if (m_curr >= m_end)
                error("Unexpected end of object");
            if (*m_curr == '}')
            {
                ++m_curr;
                return Json(std::move(obj));
            }
            if (*m_curr == ',')
                ++m_curr;
            else
                error("Expected ',' or '}' in object");
        }
    }

    void Parser::parse_object_inplace(Json &out)
    {
        ++m_curr;
        Object obj(m_resource);
        skip_whitespace();
        if (*m_curr == '}')
        {
            ++m_curr;
            out = std::move(obj);
            return;
        }

        obj.data().reserve(4);
        std::pmr::unordered_set<std::string_view> seen(m_resource);
        seen.reserve(8);

        while (true)
        {
            skip_whitespace();
            if (*m_curr != '"')
                error("Expected string key in object");
            auto key = parse_string();
            check_duplicate_key(key, seen);

            skip_whitespace();
            if (*m_curr != ':')
                error("Expected ':' in object");
            ++m_curr;

            obj.data().emplace_back(key, Json(nullptr));
            parse_value_inplace(obj.data().back().second);

            skip_whitespace();
            if (m_curr >= m_end)
                error("Unexpected end of object");
            if (*m_curr == '}')
            {
                ++m_curr;
                out = std::move(obj);
                return;
            }
            if (*m_curr == ',')
                ++m_curr;
            else
                error("Expected ',' or '}' in object");
        }
    }
}

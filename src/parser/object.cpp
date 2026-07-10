#include "pjh_json/parser.hpp"
#include "pjh_json/json.hpp"
#include <unordered_set>

namespace pjh::json
{
    // Track seen keys via unordered_set for duplicate detection.
    static void check_duplicate_key(
        std::string_view key,
        std::pmr::unordered_set<std::string_view> &seen)
    {
        if (!seen.insert(key).second)
            throw ParseError("Duplicate key in object");
    }

    Json Parser::parse_object()
    {
        Json out;
        parse_object_inplace(out);
        return out;
    }

    /*
     * Parse JSON object in-place
     *
     * 1. Consume opening '{'.
     * 2. Skip whitespace; if '}' immediately -> empty object, return.
     * 3. Pre-allocate capacity hints and set up duplicate-key tracking.
     * 4. Loop:
     *    a. Parse a string key.
     *    b. Check for duplicate keys.
     *    c. Expect and consume ':' separator.
     *    d. Parse the value in-place.
     *    e. Check for ',' (continue) or '}' (done).
     */
    void Parser::parse_object_inplace(Json &out)
    {
        // Consume '{' and create object
        ++m_curr;
        Object obj(m_resource);
        skip_whitespace();

        // Early return for empty object
        if (*m_curr == '}')
        {
            ++m_curr;
            out = std::move(obj);
            return;
        }

        // Pre-allocate to reduce reallocations
        obj.data().reserve(4);
        std::pmr::unordered_set<std::string_view> seen(m_resource);
        seen.reserve(8);

        while (true)
        {
            // Parse key
            skip_whitespace();
            if (*m_curr != '"')
                throw ParseError("Expected string key in object");
            auto key = parse_string();
            check_duplicate_key(key, seen);

            // Parse colon separator
            skip_whitespace();
            if (*m_curr != ':')
                throw ParseError("Expected ':' in object");
            ++m_curr;

            // Parse value
            obj.data().emplace_back(key, Json(nullptr));
            parse_value_inplace(obj.data().back().second);

            // Check for closing brace or comma
            skip_whitespace();
            if (m_curr >= m_end)
                throw ParseError("Unexpected end of object");
            if (*m_curr == '}')
            {
                ++m_curr;
                out = std::move(obj);
                return;
            }
            if (*m_curr == ',')
                ++m_curr;
            else
                throw ParseError("Expected ',' or '}' in object");
        }
    }
}

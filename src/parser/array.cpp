#include "pjh_json/parser.hpp"
#include "pjh_json/json.hpp"
#include "pjh_json/detail/utils.hpp"

namespace pjh::json
{
    /*
     * Parse JSON array in-place
     *
     * 1. Consume opening '['.
     * 2. Skip whitespace; if ']' immediately -> empty array, return.
     * 3. Pre-allocate capacity hint (4).
     * 4. Loop: place a null placeholder element, parse value in-place,
     *    then check for ',' (continue) or ']' (done).
     */
    void Parser::parse_array_inplace(Json &out)
    {
        if (m_max_depth != 0 && m_depth + 1 > m_max_depth)
            throw_parse_error("Maximum nesting depth exceeded", m_curr, m_begin);
        DepthFrame frame(*this);

        // Consume '[' and create array
        ++m_curr;
        Array arr(m_resource);
        skip_whitespace();

        // Early return for empty array
        if (m_curr < m_end && *m_curr == ']')
        {
            ++m_curr;
            out = std::move(arr);
            return;
        }

        // Pre-allocate: the outermost container bounds its element count
        // from the remaining input; nested containers keep the fixed hint.
        arr.data().reserve(initial_reserve(2, sizeof(Json)));

        while (true)
        {
            // Place null then overwrite via in-place parse
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
                throw_parse_error("Expected ',' or ']' in array", m_curr, m_begin);
        }
    }
}

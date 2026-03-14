#include "pjh_json/array.hpp"
#include "pjh_json/json.hpp"

namespace pjh::json
{
    void Array::push_back(Json v) { return m_data.push_back(std::move(v)); }

    bool Array::operator==(const Array &other) const noexcept
    {
        return std::equal(
            m_data.begin(),
            m_data.end(),
            other.m_data.begin(),
            [](const Json &x, const Json &y)
            { return x == y; });
    }
}
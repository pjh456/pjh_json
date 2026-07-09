#include "pjh_json/json.hpp"

namespace pjh::json
{
    Json Json::clone(std::pmr::memory_resource *into) const
    {
        Json out;
        if (is_null())
            out.m_data = nullptr;
        else if (auto b = try_as_boolean())
            out.m_data = *b;
        else if (auto i = try_as_int())
            out.m_data = *i;
        else if (auto f = try_as_float())
            out.m_data = *f;
        else if (is_string())
        {
            String s{static_cast<std::string_view>(std::get<String>(m_data))};
            s.own(into);
            out.m_data = std::move(s);
        }
        else if (const Array *a = try_as_array())
            out.m_data = a->clone(into);
        else if (const Object *o = try_as_object())
            out.m_data = o->clone(into);
        return out;
    }
}

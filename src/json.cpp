#include "pjh_json/json.hpp"

namespace pjh::json
{
    // --- operator= ---

    Json &Json::operator=(std::string_view val)
    {
        destroy();
        m_type = T_StrView;
        m_data.sv.data = val.data();
        m_data.sv.length = static_cast<uint32_t>(val.size());
        return *this;
    }

    Json &Json::operator=(const char *val)
    {
        return operator=(std::string_view(val));
    }

    Json &Json::operator=(Array &&arr) noexcept
    {
        destroy();
        m_type = T_Array;
        m_data.heap = heap_alloc(arr.m_resource, std::move(arr));
        return *this;
    }

    Json &Json::operator=(Object &&obj) noexcept
    {
        destroy();
        m_type = T_Object;
        m_data.heap = heap_alloc(obj.m_resource, std::move(obj));
        return *this;
    }

    // --- clone ---

    Json Json::clone(std::pmr::memory_resource *into) const
    {
        switch (m_type)
        {
        case T_Null:
            return nullptr;
        case T_Bool:
            return m_data.b_val;
        case T_Int:
            return m_data.i_val;
        case T_Float:
            return m_data.f_val;
        case T_StrView:
        case T_StrOwned:
        {
            std::string_view sv = as_string();
            std::pmr::string owned(sv, into);
            Json out;
            out.m_type = T_StrOwned;
            out.m_data.heap = new std::pmr::string(std::move(owned));
            return out;
        }
        case T_Array:
        {
            Json out;
            out.m_type = T_Array;
            out.m_data.heap = heap_alloc(into, as_array().clone(into));
            return out;
        }
        case T_Object:
        {
            Json out;
            out.m_type = T_Object;
            out.m_data.heap = heap_alloc(into, as_object().clone(into));
            return out;
        }
        }
        return nullptr;
    }

    // --- try_as ---

    std::optional<bool> Json::try_as_boolean() const noexcept
    {
        if (m_type == T_Bool) return m_data.b_val;
        return std::nullopt;
    }

    std::optional<int64_t> Json::try_as_int() const noexcept
    {
        if (m_type == T_Int) return m_data.i_val;
        return std::nullopt;
    }

    std::optional<double> Json::try_as_float() const noexcept
    {
        if (m_type == T_Float) return m_data.f_val;
        return std::nullopt;
    }

    std::optional<std::string_view> Json::try_as_string() const noexcept
    {
        if (is_string()) return as_string();
        return std::nullopt;
    }

    Array *Json::try_as_array() noexcept
    {
        if (m_type == T_Array) return static_cast<Array *>(m_data.heap);
        return nullptr;
    }

    const Array *Json::try_as_array() const noexcept
    {
        if (m_type == T_Array) return static_cast<const Array *>(m_data.heap);
        return nullptr;
    }

    Object *Json::try_as_object() noexcept
    {
        if (m_type == T_Object) return static_cast<Object *>(m_data.heap);
        return nullptr;
    }

    const Object *Json::try_as_object() const noexcept
    {
        if (m_type == T_Object) return static_cast<const Object *>(m_data.heap);
        return nullptr;
    }

    // --- size / empty ---

    size_t Json::size() const noexcept
    {
        if (is_array()) return as_array().size();
        if (is_object()) return as_object().size();
        return 1;
    }

    bool Json::empty() const noexcept
    {
        if (is_array()) return as_array().empty();
        if (is_object()) return as_object().empty();
        return false;
    }

    // --- operator[] / at ---

    Json &Json::operator[](size_t idx)
    {
        if (!is_array()) throw TypeError("expected array");
        return as_array()[idx];
    }

    const Json &Json::operator[](size_t idx) const
    {
        if (!is_array()) throw TypeError("expected array");
        return as_array()[idx];
    }

    Json &Json::at(size_t idx)
    {
        if (!is_array()) throw TypeError("expected array");
        return as_array().at(idx);
    }

    const Json &Json::at(size_t idx) const
    {
        if (!is_array()) throw TypeError("expected array");
        return as_array().at(idx);
    }

    Json &Json::operator[](std::string_view key)
    {
        if (!is_object()) throw TypeError("expected object");
        return as_object()[key];
    }

    const Json &Json::operator[](std::string_view key) const
    {
        if (!is_object()) throw TypeError("expected object");
        return as_object()[key];
    }

    Json &Json::at(std::string_view key)
    {
        if (!is_object()) throw TypeError("expected object");
        return as_object().at(key);
    }

    const Json &Json::at(std::string_view key) const
    {
        if (!is_object()) throw TypeError("expected object");
        return as_object().at(key);
    }

    // --- operator== ---

    bool Json::operator==(const Json &other) const noexcept
    {
        if (m_type != other.m_type) return false;
        switch (m_type)
        {
        case T_Null: return true;
        case T_Bool: return m_data.b_val == other.m_data.b_val;
        case T_Int: return m_data.i_val == other.m_data.i_val;
        case T_Float: return m_data.f_val == other.m_data.f_val;
        case T_StrView: return as_string() == other.as_string();
        case T_StrOwned: return as_string() == other.as_string();
        case T_Array: return as_array() == other.as_array();
        case T_Object: return as_object() == other.as_object();
        }
        return false;
    }

    bool Json::operator==(std::nullptr_t) const noexcept { return is_null(); }
    bool Json::operator==(bool val) const noexcept { return is_boolean() && as_boolean() == val; }
    bool Json::operator==(int64_t val) const noexcept { return is_int() && as_int() == val; }
    bool Json::operator==(double val) const noexcept { return is_float() && as_float() == val; }
    bool Json::operator==(std::string_view val) const noexcept { return is_string() && as_string() == val; }
    bool Json::operator==(const char *val) const noexcept { return operator==(std::string_view(val)); }
    bool Json::operator==(const Array &val) const noexcept { return is_array() && as_array() == val; }
    bool Json::operator==(const Object &val) const noexcept { return is_object() && as_object() == val; }
}

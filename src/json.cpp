#include "pjh_json/json.hpp"

namespace pjh::json
{
    // --- operator= ---

    Json &Json::operator=(std::nullptr_t)
    {
        m_data = nullptr;
        return *this;
    }

    Json &Json::operator=(bool val)
    {
        m_data = val;
        return *this;
    }

    Json &Json::operator=(std::string_view val)
    {
        m_data = val;
        return *this;
    }

    Json &Json::operator=(const char *val)
    {
        m_data = std::string_view(val);
        return *this;
    }

    Json &Json::operator=(Array &&arr) noexcept
    {
        m_data = std::move(arr);
        return *this;
    }

    Json &Json::operator=(Object &&obj) noexcept
    {
        m_data = std::move(obj);
        return *this;
    }

    // --- clone ---

    /*
     * Deep copy dispatching on variant type
     *
     * 1. Scalars (null/bool/int/float) copy trivially via assignment.
     * 2. String materialises into the target memory resource.
     * 3. Array/Object recurse via their own clone().
     */
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

    // --- try_as ---

    std::optional<bool> Json::try_as_boolean() const noexcept
    {
        auto *p = std::get_if<bool>(&m_data);
        return p ? std::optional(*p) : std::nullopt;
    }

    std::optional<std::int64_t> Json::try_as_int() const noexcept
    {
        auto *p = std::get_if<std::int64_t>(&m_data);
        return p ? std::optional(*p) : std::nullopt;
    }

    std::optional<double> Json::try_as_float() const noexcept
    {
        auto *p = std::get_if<double>(&m_data);
        return p ? std::optional(*p) : std::nullopt;
    }

    std::optional<std::string_view> Json::try_as_string() const noexcept
    {
        auto *p = std::get_if<String>(&m_data);
        return p ? std::optional(*p) : std::nullopt;
    }

    Array *Json::try_as_array() noexcept
    {
        return std::get_if<Array>(&m_data);
    }

    const Array *Json::try_as_array() const noexcept
    {
        return std::get_if<Array>(&m_data);
    }

    Object *Json::try_as_object() noexcept
    {
        return std::get_if<Object>(&m_data);
    }

    const Object *Json::try_as_object() const noexcept
    {
        return std::get_if<Object>(&m_data);
    }

    // --- size / empty ---

    /*
     * Delegate to contained container if array/object, else scalar
     *
     * 1. Array/object returns container size.
     * 2. Scalar always returns size=1, empty=false.
     */
    size_t Json::size() const noexcept
    {
        if (is_array())
            return as_array().size();
        if (is_object())
            return as_object().size();
        return 1;
    }

    bool Json::empty() const noexcept
    {
        if (is_array())
            return as_array().empty();
        if (is_object())
            return as_object().empty();
        return false;
    }

    // --- operator[] / at ---

    /*
     * Element access with type validation
     *
     * 1. Verify the variant holds the expected type (Array or Object).
     * 2. Delegate to the underlying container's accessor.
     *
     * operator[] skips bounds check (Array) or insert-if-missing (Object).
     * at() includes bounds check from the container.
     *
     * @throws TypeError if variant type does not match
     */

    Json &Json::operator[](size_t idx)
    {
        if (!is_array())
            throw TypeError("expected array");
        return as_array()[idx];
    }

    const Json &Json::operator[](size_t idx) const
    {
        if (!is_array())
            throw TypeError("expected array");
        return as_array()[idx];
    }

    Json &Json::at(size_t idx)
    {
        if (!is_array())
            throw TypeError("expected array");
        return as_array().at(idx);
    }

    const Json &Json::at(size_t idx) const
    {
        if (!is_array())
            throw TypeError("expected array");
        return as_array().at(idx);
    }

    Json &Json::operator[](std::string_view key)
    {
        if (!is_object())
            throw TypeError("expected object");
        return as_object()[key];
    }

    const Json &Json::operator[](std::string_view key) const
    {
        if (!is_object())
            throw TypeError("expected object");
        return as_object()[key];
    }

    Json &Json::at(std::string_view key)
    {
        if (!is_object())
            throw TypeError("expected object");
        return as_object().at(key);
    }

    const Json &Json::at(std::string_view key) const
    {
        if (!is_object())
            throw TypeError("expected object");
        return as_object().at(key);
    }

    // --- operator== ---

    /*
     * Equality comparison
     *
     * 1. Json-vs-Json: delegates to std::variant operator==.
     * 2. Json-vs-scalar: type-check first, then value comparison.
     */
    bool Json::operator==(const Json &other) const noexcept
    {
        return m_data == other.m_data;
    }

    bool Json::operator==(std::nullptr_t) const noexcept
    {
        return is_null();
    }

    bool Json::operator==(bool val) const noexcept
    {
        return is_boolean() && (as_boolean() == val);
    }

    bool Json::operator==(int64_t val) const noexcept
    {
        return is_int() && (as_int() == val);
    }

    bool Json::operator==(double val) const noexcept
    {
        return is_float() && (as_float() == val);
    }

    bool Json::operator==(std::string_view val) const noexcept
    {
        return is_string() && (as_string() == val);
    }

    bool Json::operator==(const char *val) const noexcept
    {
        return operator==(std::string_view(val));
    }

    bool Json::operator==(const Array &val) const noexcept
    {
        return is_array() && (as_array() == val);
    }

    bool Json::operator==(const Object &val) const noexcept
    {
        return is_object() && (as_object() == val);
    }
}

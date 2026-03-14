#ifndef INCLUDE_PJH_JSON_HPP
#define INCLUDE_PJH_JSON_HPP

#include <variant>
#include <cstdint>

#include "array.hpp"
#include "object.hpp"

namespace pjh::json
{

    class Json
    {

    public:
        using variant_t = std::variant<
            std::nullptr_t,
            bool,
            int64_t,
            double,
            std::string,
            Array,
            Object>;

    private:
        variant_t m_data;

    public:
        Json() = default;
        Json(std::nullptr_t) : m_data(nullptr) {}
        Json(bool val) : m_data(val) {}
        Json(int64_t val) : m_data(val) {}
        Json(double val) : m_data(val) {}
        Json(std::string str) : m_data(std::move(str)) {}
        Json(const char *str) : m_data(std::string(str)) {}
        Json(Array arr) : m_data(std::move(arr)) {}
        Json(Object obj) : m_data(std::move(obj)) {}

        Json(const Json &) = default;
        Json &operator=(const Json &) = default;

        Json(Json &&) noexcept = default;
        Json &operator=(Json &&) noexcept = default;

    public:
        bool is_null() const noexcept { return std::holds_alternative<std::nullptr_t>(m_data); }
        bool is_boolean() const noexcept { return std::holds_alternative<bool>(m_data); }
        bool is_int() const noexcept { return std::holds_alternative<std::int64_t>(m_data); }
        bool is_float() const noexcept { return std::holds_alternative<double>(m_data); }
        bool is_number() const noexcept { return is_int() || is_float(); }
        bool is_string() const noexcept { return std::holds_alternative<std::string>(m_data); }
        bool is_array() const noexcept { return std::holds_alternative<Array>(m_data); }
        bool is_object() const noexcept { return std::holds_alternative<Object>(m_data); }

    public:
        std::nullptr_t as_null() { return std::get<std::nullptr_t>(m_data); }
        bool as_boolean() { return std::get<bool>(m_data); }
        std::int64_t as_int() { return std::get<std::int64_t>(m_data); }
        double as_float() { return std::get<double>(m_data); }

        std::string &as_string() { return std::get<std::string>(m_data); }
        const std::string &as_string() const { return std::get<std::string>(m_data); }

        Array &as_array() { return std::get<Array>(m_data); }
        const Array &as_array() const { return std::get<Array>(m_data); }

        Object &as_object() { return std::get<Object>(m_data); }
        const Object &as_object() const { return std::get<Object>(m_data); }

    public:
        Json &operator[](size_t idx) { return as_array()[idx]; }
        const Json &operator[](size_t idx) const { return as_array()[idx]; }

        Json &at(size_t idx)
        {
            if (!is_array())
                throw std::runtime_error("Type error: excepted array");
            return as_array().at(idx);
        }
        const Json &at(size_t idx) const
        {
            if (!is_array())
                throw std::runtime_error("Type error: excepted array");
            return as_array().at(idx);
        }

        Json &operator[](std::string_view key) { return as_object()[key]; }
        const Json &operator[](std::string_view key) const { return as_object()[key]; }

        Json &at(std::string_view key)
        {
            if (!is_object())
                throw std::runtime_error("Type error: excepted object");
            return as_object().at(key);
        }

        const Json &at(std::string_view key) const
        {
            if (!is_object())
                throw std::runtime_error("Type error: excepted object");
            return as_object().at(key);
        }

    public:
        bool operator==(const Json &other) const noexcept { return m_data == other.m_data; }
        bool operator!=(const Json &other) const noexcept { return !(this->operator==(other)); }
    };

}

#endif // INCLUDE_PJH_JSON_HPP
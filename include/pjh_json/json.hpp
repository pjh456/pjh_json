#ifndef INCLUDE_PJH_JSON_JSON_HPP
#define INCLUDE_PJH_JSON_JSON_HPP

#include <cstdint>
#include <variant>
#include <optional>
#include <string_view>
#include <stdexcept>
#include <memory>
#include <memory_resource>
#include <utility>
#include <concepts>

#include "array.hpp"
#include "object.hpp"
#include "config.hpp"

namespace pjh::json
{

    class JsonError : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    class ParseError : public JsonError
    {
    public:
        using JsonError::JsonError;
    };

    class TypeError : public JsonError
    {
    public:
        using JsonError::JsonError;
    };

    class Json
    {

    public:
        using variant_t = std::variant<
            std::nullptr_t,
            bool,
            int64_t,
            double,
            String,
            Array,
            Object>;

    protected:
        variant_t m_data;

    public:
        Json() = default;
        Json(std::nullptr_t) : m_data(nullptr) {}
        Json(bool val) : m_data(val) {}

        template <std::integral T>
            requires(!std::same_as<T, bool>)
        Json(T val) : m_data(static_cast<int64_t>(val)) {}

        template <std::floating_point T>
        Json(T val) : m_data(static_cast<double>(val)) {}

        Json(std::string_view str) : m_data(str) {}
        Json(const char *str) : m_data(std::string_view(str)) {}
        Json(Array arr) : m_data(std::move(arr)) {}
        Json(Object obj) : m_data(std::move(obj)) {}

    public:
        Json(const Json &) = delete;
        Json &operator=(const Json &) = delete;

        Json(Json &&) noexcept = default;
        Json &operator=(Json &&) noexcept = default;

        [[nodiscard]] Json clone(
            std::pmr::memory_resource *into = Config::instance().resource()) const;

        Json &operator=(std::nullptr_t);
        Json &operator=(bool val);

        template <std::integral T>
            requires(!std::same_as<T, bool>)
        Json &operator=(T val)
        {
            m_data = static_cast<int64_t>(val);
            return *this;
        }

        template <std::floating_point T>
        Json &operator=(T val)
        {
            m_data = static_cast<double>(val);
            return *this;
        }

        Json &operator=(std::string_view val);
        Json &operator=(const char *val);
        Json &operator=(Array &&arr) noexcept;
        Json &operator=(Object &&obj) noexcept;

    public:
        [[nodiscard]] bool is_null() const noexcept { return std::holds_alternative<std::nullptr_t>(m_data); }
        [[nodiscard]] bool is_boolean() const noexcept { return std::holds_alternative<bool>(m_data); }
        [[nodiscard]] bool is_int() const noexcept { return std::holds_alternative<std::int64_t>(m_data); }
        [[nodiscard]] bool is_float() const noexcept { return std::holds_alternative<double>(m_data); }
        [[nodiscard]] bool is_number() const noexcept { return is_int() || is_float(); }
        [[nodiscard]] bool is_string() const noexcept { return std::holds_alternative<String>(m_data); }
        [[nodiscard]] bool is_array() const noexcept { return std::holds_alternative<Array>(m_data); }
        [[nodiscard]] bool is_object() const noexcept { return std::holds_alternative<Object>(m_data); }

    public:
        [[nodiscard]] std::nullptr_t as_null() { return std::get<std::nullptr_t>(m_data); }

        [[nodiscard]] bool &as_boolean() { return std::get<bool>(m_data); }
        [[nodiscard]] const bool &as_boolean() const { return std::get<bool>(m_data); }

        [[nodiscard]] std::int64_t &as_int() { return std::get<std::int64_t>(m_data); }
        [[nodiscard]] const std::int64_t &as_int() const { return std::get<std::int64_t>(m_data); }

        [[nodiscard]] double &as_float() { return std::get<double>(m_data); }
        [[nodiscard]] const double &as_float() const { return std::get<double>(m_data); }

        [[nodiscard]] std::string_view as_string() { return std::get<String>(m_data); }
        [[nodiscard]] std::string_view as_string() const { return std::get<String>(m_data); }

        [[nodiscard]] Array &as_array() { return std::get<Array>(m_data); }
        [[nodiscard]] const Array &as_array() const { return std::get<Array>(m_data); }

        [[nodiscard]] Object &as_object() { return std::get<Object>(m_data); }
        [[nodiscard]] const Object &as_object() const { return std::get<Object>(m_data); }

    public:
        [[nodiscard]] std::optional<bool> try_as_boolean() const noexcept;
        [[nodiscard]] std::optional<std::int64_t> try_as_int() const noexcept;
        [[nodiscard]] std::optional<double> try_as_float() const noexcept;
        [[nodiscard]] std::optional<std::string_view> try_as_string() const noexcept;
        [[nodiscard]] Array *try_as_array() noexcept;
        [[nodiscard]] const Array *try_as_array() const noexcept;
        [[nodiscard]] Object *try_as_object() noexcept;
        [[nodiscard]] const Object *try_as_object() const noexcept;

    public:
        [[nodiscard]] size_t size() const noexcept;
        [[nodiscard]] bool empty() const noexcept;

    public:
        Json &operator[](size_t idx);
        const Json &operator[](size_t idx) const;
        Json &at(size_t idx);
        const Json &at(size_t idx) const;
        Json &operator[](std::string_view key);
        const Json &operator[](std::string_view key) const;
        Json &at(std::string_view key);
        const Json &at(std::string_view key) const;

    public:
        [[nodiscard]] bool operator==(const Json &other) const noexcept;
        [[nodiscard]] bool operator==(std::nullptr_t) const noexcept;
        [[nodiscard]] bool operator==(bool val) const noexcept;
        [[nodiscard]] bool operator==(int64_t val) const noexcept;
        [[nodiscard]] bool operator==(double val) const noexcept;
        [[nodiscard]] bool operator==(std::string_view val) const noexcept;
        [[nodiscard]] bool operator==(const char *val) const noexcept;
        [[nodiscard]] bool operator==(const Array &val) const noexcept;
        [[nodiscard]] bool operator==(const Object &val) const noexcept;
    };
}

#endif // INCLUDE_PJH_JSON_JSON_HPP

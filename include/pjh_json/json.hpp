#ifndef INCLUDE_PJH_JSON_HPP
#define INCLUDE_PJH_JSON_HPP

#include <cstdint>
#include <variant>
#include <optional>
#include <initializer_list>
#include <string_view>
#include <stdexcept>
#include <memory_resource>

#include "array.hpp"
#include "object.hpp"
#include "config.hpp"

namespace pjh::json
{

    // -----------------------------------------------------------------------
    // Exception hierarchy
    // -----------------------------------------------------------------------
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
        Json(int64_t val) : m_data(val) {}
        Json(double val) : m_data(val) {}
        Json(std::string_view str) : m_data(str) {}
        Json(const char *str) : m_data(std::string_view(str)) {}
        Json(Array arr) : m_data(std::move(arr)) {}
        Json(Object obj) : m_data(std::move(obj)) {}
        Json(std::initializer_list<Json> vec)
            : m_data(Array(vec)) {}
        Json(std::initializer_list<Object::Entry> items)
            : m_data(Object(items)) {}

    public:
        Json(const Json &) = default;
        Json &operator=(const Json &) = default;

        Json(Json &&) noexcept = default;
        Json &operator=(Json &&) noexcept = default;

        Json &operator=(std::nullptr_t)
        {
            m_data = nullptr;
            return *this;
        }

        Json &operator=(bool val)
        {
            m_data = val;
            return *this;
        }

        Json &operator=(int64_t val)
        {
            m_data = val;
            return *this;
        }

        Json &operator=(double val)
        {
            m_data = val;
            return *this;
        }

        Json &operator=(std::string_view val)
        {
            m_data = val;
            return *this;
        }

        Json &operator=(const char *val)
        {
            m_data = std::string_view(val);
            return *this;
        }

        Json &operator=(const Array &arr)
        {
            m_data = arr;
            return *this;
        }

        Json &operator=(Array &&arr) noexcept
        {
            m_data = std::move(arr);
            return *this;
        }

        Json &operator=(const Object &obj)
        {
            m_data = obj;
            return *this;
        }

        Json &operator=(Object &&obj) noexcept
        {
            m_data = std::move(obj);
            return *this;
        }

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
        [[nodiscard]] std::optional<bool> try_as_boolean() const noexcept
        {
            auto *p = std::get_if<bool>(&m_data);
            return p ? std::optional(*p) : std::nullopt;
        }
        [[nodiscard]] std::optional<std::int64_t> try_as_int() const noexcept
        {
            auto *p = std::get_if<std::int64_t>(&m_data);
            return p ? std::optional(*p) : std::nullopt;
        }
        [[nodiscard]] std::optional<double> try_as_float() const noexcept
        {
            auto *p = std::get_if<double>(&m_data);
            return p ? std::optional(*p) : std::nullopt;
        }
        [[nodiscard]] std::optional<std::string_view> try_as_string() const noexcept
        {
            auto *p = std::get_if<String>(&m_data);
            return p ? std::optional(*p) : std::nullopt;
        }
        [[nodiscard]] Array *try_as_array() noexcept
        {
            return std::get_if<Array>(&m_data);
        }
        [[nodiscard]] const Array *try_as_array() const noexcept
        {
            return std::get_if<Array>(&m_data);
        }
        [[nodiscard]] Object *try_as_object() noexcept
        {
            return std::get_if<Object>(&m_data);
        }
        [[nodiscard]] const Object *try_as_object() const noexcept
        {
            return std::get_if<Object>(&m_data);
        }

    public:
        [[nodiscard]] size_t size() const noexcept
        {
            if (is_array())
                return as_array().size();
            if (is_object())
                return as_object().size();
            return 1;
        }
        [[nodiscard]] bool empty() const noexcept
        {
            if (is_array())
                return as_array().empty();
            if (is_object())
                return as_object().empty();
            return false;
        }

    public:
        Json &operator[](size_t idx)
        {
            if (!is_array())
                throw TypeError("expected array");
            return as_array()[idx];
        }
        const Json &operator[](size_t idx) const
        {
            if (!is_array())
                throw TypeError("expected array");
            return as_array()[idx];
        }

        Json &at(size_t idx)
        {
            if (!is_array())
                throw TypeError("expected array");
            return as_array().at(idx);
        }
        const Json &at(size_t idx) const
        {
            if (!is_array())
                throw TypeError("expected array");
            return as_array().at(idx);
        }

        Json &operator[](std::string_view key)
        {
            if (!is_object())
                throw TypeError("expected object");
            return as_object()[key];
        }
        const Json &operator[](std::string_view key) const
        {
            if (!is_object())
                throw TypeError("expected object");
            return as_object()[key];
        }

        Json &at(std::string_view key)
        {
            if (!is_object())
                throw TypeError("expected object");
            return as_object().at(key);
        }

        const Json &at(std::string_view key) const
        {
            if (!is_object())
                throw TypeError("expected object");
            return as_object().at(key);
        }

    public:
        [[nodiscard]] bool operator==(const Json &other) const noexcept { return m_data == other.m_data; }

        [[nodiscard]] bool operator==(std::nullptr_t) const noexcept { return is_null(); }

        [[nodiscard]] bool operator==(bool val) const noexcept { return is_boolean() && (as_boolean() == val); }

        [[nodiscard]] bool operator==(int64_t val) const noexcept { return is_int() && (as_int() == val); }

        [[nodiscard]] bool operator==(double val) const noexcept { return is_float() && (as_float() == val); }

        [[nodiscard]] bool operator==(std::string_view val) const noexcept { return is_string() && (as_string() == val); }

        [[nodiscard]] bool operator==(const char *val) const noexcept { return operator==(std::string_view(val)); }

        [[nodiscard]] bool operator==(const Array &val) const noexcept { return is_array() && (as_array() == val); }

        [[nodiscard]] bool operator==(const Object &val) const noexcept { return is_object() && (as_object() == val); }
    };

    // ---------------------------------------------------------
    // Document — Zero-copy + Owning JSON root
    // ---------------------------------------------------------
    class Document
    {
        Json m_root;
        std::pmr::string m_buffer;          // null/empty = view mode

    public:
        Document() = default;

        // Owned mode: buffer is owned (parse_in_situ, parse_copy, parse_file)
        Document(Json &&js, std::pmr::string &&buf)
            : m_root(std::move(js)), m_buffer(std::move(buf)) {}

        // View mode: caller keeps the backing buffer alive (parse_view)
        explicit Document(Json &&js)
            : m_root(std::move(js)) {}

        Document(const Document &) = delete;
        Document &operator=(const Document &) = delete;
        Document(Document &&) noexcept = default;
        Document &operator=(Document &&) noexcept = default;

        [[nodiscard]] bool is_view() const noexcept { return m_buffer.empty(); }
        [[nodiscard]] const std::pmr::string &buffer() const noexcept { return m_buffer; }
        [[nodiscard]] Json &root() noexcept { return m_root; }
        [[nodiscard]] const Json &root() const noexcept { return m_root; }
    };

    [[nodiscard]] Document parse_in_situ(
        std::pmr::string &&buffer,
        std::pmr::memory_resource *res = Config::instance().resource());

    [[nodiscard]] Document parse_copy(
        std::string_view json,
        std::pmr::memory_resource *res = Config::instance().resource());

    [[nodiscard]] Document parse_view(
        const char *data, size_t content_len,
        std::pmr::memory_resource *res = Config::instance().resource());

    [[nodiscard]] Document parse_jsonl(
        std::string_view input,
        std::pmr::memory_resource *res = Config::instance().resource());

    [[nodiscard]] Document parse_file(
        std::string_view filepath,
        std::pmr::memory_resource *res = Config::instance().resource());
}

#endif // INCLUDE_PJH_JSON_HPP

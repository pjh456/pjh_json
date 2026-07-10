#ifndef INCLUDE_PJH_JSON_JSON_HPP
#define INCLUDE_PJH_JSON_JSON_HPP

#include <cstdint>
#include <optional>
#include <string_view>
#include <stdexcept>
#include <memory>
#include <memory_resource>
#include <utility>
#include <concepts>
#include <new>

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

    /**
     * @brief Core JSON value type (custom tagged union, 24 bytes)
     *
     * Holds one of: null, bool, int64, double, string (borrowed or owned),
     * Array*, Object*. Array and Object are heap-allocated via PMR.
     * Copy disabled -- use clone() for deep copy.
     */
    class Json
    {
    public:
        enum Type : uint8_t
        {
            T_Null = 0,
            T_Bool,
            T_Int,
            T_Float,
            T_StrView,
            T_StrOwned,
            T_Array,
            T_Object
        };

    private:
        Type m_type = T_Null;

        struct StrViewData
        {
            const char *data;
            uint32_t length;
        };

        union Data
        {
            bool b_val;
            int64_t i_val;
            double f_val;
            StrViewData sv;
            void *heap;
        } m_data = {};

        // --- heap allocation helpers ---

        void destroy()
        {
            switch (m_type)
            {
            case T_StrOwned:
                delete static_cast<std::pmr::string *>(m_data.heap);
                break;
            case T_Array:
            {
                std::pmr::polymorphic_allocator<Array> alloc(
                    static_cast<Array *>(m_data.heap)->m_resource);
                auto *p = static_cast<Array *>(m_data.heap);
                std::destroy_at(p);
                alloc.deallocate(p, 1);
                break;
            }
            case T_Object:
            {
                std::pmr::polymorphic_allocator<Object> alloc(
                    static_cast<Object *>(m_data.heap)->m_resource);
                auto *p = static_cast<Object *>(m_data.heap);
                std::destroy_at(p);
                alloc.deallocate(p, 1);
                break;
            }
            default:
                break;
            }
            m_type = T_Null;
        }

        template <typename T>
        static T *heap_alloc(std::pmr::memory_resource *res, T &&val)
        {
            std::pmr::polymorphic_allocator<T> alloc(res);
            T *p = alloc.allocate(1);
            std::construct_at(p, std::move(val));
            return p;
        }

    public:
        Json() = default;
        Json(std::nullptr_t) {}

        Json(bool val) : m_type(T_Bool) { m_data.b_val = val; }
        Json(int64_t val) : m_type(T_Int) { m_data.i_val = val; }
        Json(double val) : m_type(T_Float) { m_data.f_val = val; }

        template <std::integral T>
            requires(!std::same_as<T, bool>)
        Json(T val) : m_type(T_Int), m_data{.i_val = static_cast<int64_t>(val)} {}

        template <std::floating_point T>
        Json(T val) : m_type(T_Float), m_data{.f_val = static_cast<double>(val)} {}

        Json(std::string_view sv) : m_type(T_StrView)
        {
            m_data.sv.data = sv.data();
            m_data.sv.length = static_cast<uint32_t>(sv.size());
        }

        Json(const char *str) : Json(std::string_view(str)) {}

        Json(Array arr) : m_type(T_Array)
        {
            m_data.heap = heap_alloc(arr.m_resource, std::move(arr));
        }

        Json(Object obj) : m_type(T_Object)
        {
            m_data.heap = heap_alloc(obj.m_resource, std::move(obj));
        }

        /**
         * @brief Construct from String (used by parser for string values)
         */
        Json(String &&s) : m_type(T_StrView)
        {
            std::string_view sv = s;
            m_data.sv.data = sv.data();
            m_data.sv.length = static_cast<uint32_t>(sv.size());

            if (s.is_owned())
            {
                m_type = T_StrOwned;
                m_data.heap = s.release();
            }
        }

    public:
        Json(const Json &) = delete;
        Json &operator=(const Json &) = delete;

        Json(Json &&other) noexcept : m_type(other.m_type)
        {
            m_data = other.m_data;
            other.m_type = T_Null;
        }

        Json &operator=(Json &&other) noexcept
        {
            if (this != &other)
            {
                destroy();
                m_type = other.m_type;
                m_data = other.m_data;
                other.m_type = T_Null;
            }
            return *this;
        }

        ~Json() { destroy(); }

        /**
         * @brief Deep copy into specified memory resource
         */
        [[nodiscard]] Json clone(
            std::pmr::memory_resource *into = Config::instance().resource()) const;

        // --- assignment operators ---

        Json &operator=(std::nullptr_t)
        {
            destroy();
            return *this;
        }

        Json &operator=(bool val)
        {
            destroy();
            m_type = T_Bool;
            m_data.b_val = val;
            return *this;
        }

        Json &operator=(String &&s)
        {
            destroy();
            std::string_view sv = s;
            m_data.sv.data = sv.data();
            m_data.sv.length = static_cast<uint32_t>(sv.size());
            if (s.is_owned())
            {
                m_type = T_StrOwned;
                m_data.heap = s.release();
            }
            else
                m_type = T_StrView;
            return *this;
        }

        template <std::integral T>
            requires(!std::same_as<T, bool>)
        Json &operator=(T val)
        {
            destroy();
            m_type = T_Int;
            m_data.i_val = static_cast<int64_t>(val);
            return *this;
        }

        template <std::floating_point T>
        Json &operator=(T val)
        {
            destroy();
            m_type = T_Float;
            m_data.f_val = static_cast<double>(val);
            return *this;
        }

        Json &operator=(std::string_view val);
        Json &operator=(const char *val);
        Json &operator=(Array &&arr) noexcept;
        Json &operator=(Object &&obj) noexcept;

        // --- type checks ---

        [[nodiscard]] bool is_null() const noexcept { return m_type == T_Null; }
        [[nodiscard]] bool is_boolean() const noexcept { return m_type == T_Bool; }
        [[nodiscard]] bool is_int() const noexcept { return m_type == T_Int; }
        [[nodiscard]] bool is_float() const noexcept { return m_type == T_Float; }
        [[nodiscard]] bool is_number() const noexcept { return is_int() || is_float(); }
        [[nodiscard]] bool is_string() const noexcept { return m_type == T_StrView || m_type == T_StrOwned; }
        [[nodiscard]] bool is_array() const noexcept { return m_type == T_Array; }
        [[nodiscard]] bool is_object() const noexcept { return m_type == T_Object; }

        // --- unchecked access ---

        [[nodiscard]] std::nullptr_t as_null() const { return nullptr; }

        [[nodiscard]] bool &as_boolean()
        {
            return m_data.b_val;
        }
        [[nodiscard]] const bool &as_boolean() const { return m_data.b_val; }

        [[nodiscard]] int64_t &as_int()
        {
            return m_data.i_val;
        }
        [[nodiscard]] const int64_t &as_int() const { return m_data.i_val; }

        [[nodiscard]] double &as_float()
        {
            return m_data.f_val;
        }
        [[nodiscard]] const double &as_float() const { return m_data.f_val; }

        [[nodiscard]] std::string_view as_string() const
        {
            if (m_type == T_StrView)
                return std::string_view(m_data.sv.data, m_data.sv.length);
            return *static_cast<std::pmr::string *>(m_data.heap);
        }

        [[nodiscard]] Array &as_array()
        {
            return *static_cast<Array *>(m_data.heap);
        }
        [[nodiscard]] const Array &as_array() const
        {
            return *static_cast<const Array *>(m_data.heap);
        }

        [[nodiscard]] Object &as_object()
        {
            return *static_cast<Object *>(m_data.heap);
        }
        [[nodiscard]] const Object &as_object() const
        {
            return *static_cast<const Object *>(m_data.heap);
        }

        // --- safe access ---

        [[nodiscard]] std::optional<bool> try_as_boolean() const noexcept;
        [[nodiscard]] std::optional<int64_t> try_as_int() const noexcept;
        [[nodiscard]] std::optional<double> try_as_float() const noexcept;
        [[nodiscard]] std::optional<std::string_view> try_as_string() const noexcept;
        [[nodiscard]] Array *try_as_array() noexcept;
        [[nodiscard]] const Array *try_as_array() const noexcept;
        [[nodiscard]] Object *try_as_object() noexcept;
        [[nodiscard]] const Object *try_as_object() const noexcept;

        // --- container access ---

        [[nodiscard]] size_t size() const noexcept;
        [[nodiscard]] bool empty() const noexcept;

        Json &operator[](size_t idx);
        const Json &operator[](size_t idx) const;
        Json &at(size_t idx);
        const Json &at(size_t idx) const;

        Json &operator[](std::string_view key);
        const Json &operator[](std::string_view key) const;
        Json &at(std::string_view key);
        const Json &at(std::string_view key) const;

        // --- comparison ---

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

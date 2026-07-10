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

    /**
     * @brief Base exception for all JSON errors
     */
    class JsonError : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    /**
     * @brief JSON parse failure
     */
    class ParseError : public JsonError
    {
    public:
        using JsonError::JsonError;
    };

    /**
     * @brief Type mismatch on access
     */
    class TypeError : public JsonError
    {
    public:
        using JsonError::JsonError;
    };

    /**
     * @brief Core JSON value type (custom tagged union)
     *
     * Replaces std::variant with a hand-rolled tagged union to reduce
     * per-node size from 48 to 24 bytes. Small types (null, bool, int64,
     * double, borrowed string) are stored inline. Heap types (owned string,
     * Array, Object) are stored as pointers into PMR-allocated memory.
     *
     * Copy disabled -- use clone() for deep copy.
     */
    class Json
    {
    public:
        /**
         * @brief Internal type tag
         *
         * T_StrView: borrowed string_view stored inline as {ptr, len}.
         * T_StrOwned: heap-allocated pmr::string, pointer in m_data.heap.
         * T_Array/T_Object: heap-allocated via PMR, pointer in m_data.heap.
         */
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

        /*
         * Destroy current value and reset to null.
         *
         * 1. Scalar types (null/bool/int/float/StrView): nothing to free.
         * 2. T_StrOwned: delete the heap-allocated pmr::string.
         * 3. T_Array/T_Object: destroy in place, then deallocate via PMR
         *    using the resource stored in the container's m_resource field.
         */
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

        /*
         * Allocate and construct a heap value via PMR.
         *
         * 1. Create a polymorphic_allocator<T> from the given resource.
         * 2. Allocate one T.
         * 3. Construct T in place via std::construct_at (move semantics).
         */
        template <typename T>
        static T *heap_alloc(std::pmr::memory_resource *res, T &&val)
        {
            std::pmr::polymorphic_allocator<T> alloc(res);
            T *p = alloc.allocate(1);
            std::construct_at(p, std::move(val));
            return p;
        }

    public:
        /**
         * @brief Construct null value
         */
        Json() = default;

        /**
         * @brief Construct null value
         */
        Json(std::nullptr_t) {}

        /**
         * @brief Construct bool value
         * @param val Boolean to store
         */
        Json(bool val) : m_type(T_Bool) { m_data.b_val = val; }

        /**
         * @brief Construct int64 value
         * @param val Integer to store
         */
        Json(int64_t val) : m_type(T_Int) { m_data.i_val = val; }

        /**
         * @brief Construct double value
         * @param val Floating-point to store
         */
        Json(double val) : m_type(T_Float) { m_data.f_val = val; }

        /**
         * @brief Construct int64 from any integer type (except bool)
         * @tparam T Integral type
         * @param val Value to store as int64
         */
        template <std::integral T>
            requires(!std::same_as<T, bool>)
        Json(T val) : m_type(T_Int), m_data{.i_val = static_cast<int64_t>(val)} {}

        /**
         * @brief Construct double from any floating-point type
         * @tparam T Floating-point type
         * @param val Value to store as double
         */
        template <std::floating_point T>
        Json(T val) : m_type(T_Float), m_data{.f_val = static_cast<double>(val)} {}

        /**
         * @brief Construct string from string_view (borrowed)
         * @param sv Source view -- caller must guarantee lifetime
         * @note Does NOT copy. The Json borrows a {ptr, len} pair.
         */
        Json(std::string_view sv) : m_type(T_StrView)
        {
            m_data.sv.data = sv.data();
            m_data.sv.length = static_cast<uint32_t>(sv.size());
        }

        /**
         * @brief Construct string from C string (borrowed view)
         * @param str NUL-terminated source -- caller must guarantee lifetime
         * @note Does NOT copy. Internally wraps str in string_view.
         */
        Json(const char *str) : Json(std::string_view(str)) {}

        /**
         * @brief Construct array value (takes ownership, heap-allocated)
         * @param arr Array to move into this value
         */
        Json(Array arr) : m_type(T_Array)
        {
            m_data.heap = heap_alloc(arr.m_resource, std::move(arr));
        }

        /**
         * @brief Construct object value (takes ownership, heap-allocated)
         * @param obj Object to move into this value
         */
        Json(Object obj) : m_type(T_Object)
        {
            m_data.heap = heap_alloc(obj.m_resource, std::move(obj));
        }

        /**
         * @brief Construct from String rvalue (used by parser).
         *
         * Borrowed strings store {ptr, len} inline. Owned strings
         * transfer the heap-allocated pmr::string pointer via release().
         *
         * @param s String to adopt (must be an rvalue)
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
        /**
         * @brief Copy not allowed -- use clone()
         */
        Json(const Json &) = delete;
        /**
         * @brief Copy not allowed -- use clone()
         */
        Json &operator=(const Json &) = delete;

        /**
         * @brief Move construct (steals heap pointers, marks source null)
         */
        Json(Json &&other) noexcept : m_type(other.m_type)
        {
            m_data = other.m_data;
            other.m_type = T_Null;
        }

        /**
         * @brief Move assign (destroys old value, steals from source)
         */
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

        /**
         * @brief Destructor -- frees heap-allocated types
         */
        ~Json() { destroy(); }

        /**
         * @brief Deep copy into specified memory resource
         * @param into Allocator for copied values (default: global config resource)
         * @return Independent deep copy
         * @note Strings are materialised (owned) in the target resource.
         */
        [[nodiscard]] Json clone(
            std::pmr::memory_resource *into = Config::instance().resource()) const;

        /**
         * @brief Assign null
         * @return *this
         */
        Json &operator=(std::nullptr_t)
        {
            destroy();
            return *this;
        }

        /**
         * @brief Assign bool
         * @param val Boolean to assign
         * @return *this
         */
        Json &operator=(bool val)
        {
            destroy();
            m_type = T_Bool;
            m_data.b_val = val;
            return *this;
        }

        /**
         * @brief Assign from String rvalue
         *
         * Borrowed strings store {ptr, len} inline. Owned strings
         * transfer the heap pointer. Old value is destroyed first.
         *
         * @param s Source String (rvalue, ownership transferred)
         * @return *this
         */
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

        /**
         * @brief Assign integer (stores as int64)
         * @tparam T Integral type
         * @param val Value to assign
         * @return *this
         */
        template <std::integral T>
            requires(!std::same_as<T, bool>)
        Json &operator=(T val)
        {
            destroy();
            m_type = T_Int;
            m_data.i_val = static_cast<int64_t>(val);
            return *this;
        }

        /**
         * @brief Assign floating-point (stores as double)
         * @tparam T Floating-point type
         * @param val Value to assign
         * @return *this
         */
        template <std::floating_point T>
        Json &operator=(T val)
        {
            destroy();
            m_type = T_Float;
            m_data.f_val = static_cast<double>(val);
            return *this;
        }

        /**
         * @brief Assign string_view (borrowed)
         * @param val Source view -- caller must guarantee lifetime
         * @return *this
         * @note Does NOT copy.
         */
        Json &operator=(std::string_view val);

        /**
         * @brief Assign C string (borrowed view)
         * @param val NUL-terminated source -- caller must guarantee lifetime
         * @return *this
         * @note Does NOT copy. Internally wraps val in string_view.
         */
        Json &operator=(const char *val);

        /**
         * @brief Assign array (takes ownership, heap-allocated)
         * @param arr Array to move
         * @return *this
         */
        Json &operator=(Array &&arr) noexcept;

        /**
         * @brief Assign object (takes ownership, heap-allocated)
         * @param obj Object to move
         * @return *this
         */
        Json &operator=(Object &&obj) noexcept;

    public:
        /** @name Type checks */
        /**@{*/

        /**
         * @brief true if holds null
         */
        [[nodiscard]] bool is_null() const noexcept { return m_type == T_Null; }

        /**
         * @brief true if holds bool
         */
        [[nodiscard]] bool is_boolean() const noexcept { return m_type == T_Bool; }

        /**
         * @brief true if holds int64
         */
        [[nodiscard]] bool is_int() const noexcept { return m_type == T_Int; }

        /**
         * @brief true if holds double
         */
        [[nodiscard]] bool is_float() const noexcept { return m_type == T_Float; }

        /**
         * @brief true if variant is a number (int or float)
         */
        [[nodiscard]] bool is_number() const noexcept { return is_int() || is_float(); }

        /**
         * @brief true if holds a string (borrowed or owned)
         */
        [[nodiscard]] bool is_string() const noexcept { return m_type == T_StrView || m_type == T_StrOwned; }

        /**
         * @brief true if holds Array
         */
        [[nodiscard]] bool is_array() const noexcept { return m_type == T_Array; }

        /**
         * @brief true if holds Object
         */
        [[nodiscard]] bool is_object() const noexcept { return m_type == T_Object; }
        /**@}*/

    public:
        /** @name Unchecked access (no type validation) */
        /**@{*/

        /**
         * @brief Get null value
         */
        [[nodiscard]] std::nullptr_t as_null() const { return nullptr; }

        /**
         * @brief Get bool reference
         */
        [[nodiscard]] bool &as_boolean() { return m_data.b_val; }
        /**
         * @brief Get const bool reference
         */
        [[nodiscard]] const bool &as_boolean() const { return m_data.b_val; }

        /**
         * @brief Get int64 reference
         */
        [[nodiscard]] int64_t &as_int() { return m_data.i_val; }
        /**
         * @brief Get const int64 reference
         */
        [[nodiscard]] const int64_t &as_int() const { return m_data.i_val; }

        /**
         * @brief Get double reference
         */
        [[nodiscard]] double &as_float() { return m_data.f_val; }
        /**
         * @brief Get const double reference
         */
        [[nodiscard]] const double &as_float() const { return m_data.f_val; }

        /**
         * @brief Get string view.
         *
         * For T_StrView, returns a view of the inline {ptr, len} pair.
         * For T_StrOwned, returns a view of the heap-allocated pmr::string.
         *
         * @return View of the string content
         */
        [[nodiscard]] std::string_view as_string() const
        {
            if (m_type == T_StrView)
                return std::string_view(m_data.sv.data, m_data.sv.length);
            return *static_cast<std::pmr::string *>(m_data.heap);
        }

        /**
         * @brief Get array reference
         */
        [[nodiscard]] Array &as_array() { return *static_cast<Array *>(m_data.heap); }
        /**
         * @brief Get const array reference
         */
        [[nodiscard]] const Array &as_array() const { return *static_cast<const Array *>(m_data.heap); }

        /**
         * @brief Get object reference
         */
        [[nodiscard]] Object &as_object() { return *static_cast<Object *>(m_data.heap); }
        /**
         * @brief Get const object reference
         */
        [[nodiscard]] const Object &as_object() const { return *static_cast<const Object *>(m_data.heap); }
        /**@}*/

    public:
        /**
         * @name Safe access (returns nullopt/nullptr on type mismatch)
         */
        /**@{*/
        /**
         * @brief Try get bool
         * @return nullopt if not bool
         */
        [[nodiscard]] std::optional<bool> try_as_boolean() const noexcept;
        /**
         * @brief Try get int64
         * @return nullopt if not int64
         */
        [[nodiscard]] std::optional<int64_t> try_as_int() const noexcept;
        /**
         * @brief Try get double
         * @return nullopt if not double
         */
        [[nodiscard]] std::optional<double> try_as_float() const noexcept;
        /**
         * @brief Try get string view
         * @return nullopt if not a string
         */
        [[nodiscard]] std::optional<std::string_view> try_as_string() const noexcept;
        /**
         * @brief Try get array pointer
         * @return nullptr if not Array
         */
        [[nodiscard]] Array *try_as_array() noexcept;
        /**
         * @brief Try get const array pointer
         * @return nullptr if not Array
         */
        [[nodiscard]] const Array *try_as_array() const noexcept;
        /**
         * @brief Try get object pointer
         * @return nullptr if not Object
         */
        [[nodiscard]] Object *try_as_object() noexcept;
        /**
         * @brief Try get const object pointer
         * @return nullptr if not Object
         */
        [[nodiscard]] const Object *try_as_object() const noexcept;
        /**@}*/

    public:
        /**
         * @brief Element count
         * @return For array: size(); for object: size(); for scalar: 1
         */
        [[nodiscard]] size_t size() const noexcept;

        /**
         * @brief true if array or object is empty
         * @return For array/object: empty(); for scalar: false
         * @note Scalars never return true.
         */
        [[nodiscard]] bool empty() const noexcept;

    public:
        /** @name Array element access */
        /**@{*/
        /**
         * @brief Index access (no bounds check)
         * @param idx Element index
         * @throws TypeError if not an Array
         */
        Json &operator[](size_t idx);
        /**
         * @brief Const index access (no bounds check)
         * @param idx Element index
         * @throws TypeError if not an Array
         */
        const Json &operator[](size_t idx) const;
        /**
         * @brief Index access with bounds check
         * @param idx Element index
         * @throws TypeError if not an Array
         * @throws std::out_of_range if idx >= size()
         */
        Json &at(size_t idx);
        /**
         * @brief Const index access with bounds check
         * @param idx Element index
         * @throws TypeError if not an Array
         * @throws std::out_of_range if idx >= size()
         */
        const Json &at(size_t idx) const;
        /**@}*/

        /**
         * @name Object field access
         * @throws TypeError if not an Object
         */
        /**@{*/
        /**
         * @brief Access or insert key
         * @param key Field name
         * @throws TypeError if not an Object
         * @note Missing key is default-constructed in place.
         */
        Json &operator[](std::string_view key);
        /**
         * @brief Const access key
         * @param key Field name
         * @throws TypeError if not an Object
         * @throws std::out_of_range if key not found
         */
        const Json &operator[](std::string_view key) const;
        /**
         * @brief Access key with bounds check
         * @param key Field name
         * @throws TypeError if not an Object
         * @throws std::out_of_range if key not found
         */
        Json &at(std::string_view key);
        /**
         * @brief Const access key with bounds check
         * @param key Field name
         * @throws TypeError if not an Object
         * @throws std::out_of_range if key not found
         */
        const Json &at(std::string_view key) const;
        /**@}*/

    public:
        /** @name Comparison */
        /**@{*/
        /**
         * @brief Compare type and value with another Json
         * @param other Json to compare with
         * @return true if same type and value
         */
        [[nodiscard]] bool operator==(const Json &other) const noexcept;
        /**
         * @brief Compare with null
         * @return true if holds null
         */
        [[nodiscard]] bool operator==(std::nullptr_t) const noexcept;
        /**
         * @brief Compare with bool
         * @return true if holds same bool
         */
        [[nodiscard]] bool operator==(bool val) const noexcept;
        /**
         * @brief Compare with int64
         * @return true if holds same int64
         */
        [[nodiscard]] bool operator==(int64_t val) const noexcept;
        /**
         * @brief Compare with double
         * @return true if holds same double
         */
        [[nodiscard]] bool operator==(double val) const noexcept;
        /**
         * @brief Compare with string_view
         * @return true if holds matching String
         */
        [[nodiscard]] bool operator==(std::string_view val) const noexcept;
        /**
         * @brief Compare with C string
         * @return true if holds matching String
         */
        [[nodiscard]] bool operator==(const char *val) const noexcept;
        /**
         * @brief Compare with Array
         * @return true if holds equal Array
         */
        [[nodiscard]] bool operator==(const Array &val) const noexcept;
        /**
         * @brief Compare with Object
         * @return true if holds equal Object
         */
        [[nodiscard]] bool operator==(const Object &val) const noexcept;
        /**@}*/
    };

}

#endif // INCLUDE_PJH_JSON_JSON_HPP

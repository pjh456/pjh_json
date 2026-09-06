#ifndef INCLUDE_PJH_JSON_JSON_HPP
#define INCLUDE_PJH_JSON_JSON_HPP

#include <cstdint>
#include <optional>
#include <string_view>
#include <string>
#include <stdexcept>
#include <memory>
#include <memory_resource>
#include <type_traits>
#include <utility>
#include <concepts>
#include <new>

#include "array.hpp"
#include "object.hpp"
#include "config.hpp"
#include "utils.hpp"

namespace pjh::json
{

    /**
     * @brief Core JSON value type — custom tagged union, 24 bytes.
     *
     * Replaces std::variant with a hand-rolled tagged union to reduce
     * per-node size from 48 to 24 bytes. Small types (null, bool, int64,
     * double, borrowed string) are stored inline. Heap types (owned string,
     * Array, Object) are stored as pointers into PMR-allocated memory.
     *
     * Move-only — copy disabled. Use clone() for deep copy.
     */
    class Json
    {
    public:
        /**
         * @brief Runtime type tag for the active union member.
         */
        enum class Type : uint8_t
        {
            Null = 0,    // nullptr_t
            Boolean,     // bool
            Integer,     // int64_t
            Floating,    // double
            StringView,  // borrowed: m_data.str_view = {ptr, len}
            StringOwned, // owned:   m_data.heap → pmr::string*
            ArrayType,   // m_data.heap → Array*
            ObjectType   // m_data.heap → Object*
        };

    private:
        Type m_type = Type::Null;

        struct StrView
        {
            const char *data;
            uint32_t length;
        };

        /**
         * @brief Value storage. Only one member is active, selected by m_type.
         *
         * | m_type             | active member |
         * |--------------------|---------------|
         * | Null               | (none)        |
         * | Boolean            | boolean       |
         * | Integer            | integer       |
         * | Floating           | floating      |
         * | StringView         | str_view      |
         * | StringOwned        | heap (pmr::string*)  |
         * | ArrayType          | heap (Array*)        |
         * | ObjectType         | heap (Object*)       |
         */
        union Data
        {
            bool boolean;
            int64_t integer;
            double floating;
            StrView str_view;
            void *heap;
        } m_data = {};

        /*
         * Destroy current value and reset to Null.
         *
         * 1. Scalar types (null/boolean/integer/floating/StringView):
         *    nothing to free.
         * 2. StringOwned: delete the heap-allocated pmr::string.
         * 3. ArrayType/ObjectType: destroy in place, then deallocate
         *    via PMR using the resource stored in the container.
         */
        void destroy()
        {
            switch (m_type)
            {
            case Type::StringOwned:
                delete static_cast<std::pmr::string *>(m_data.heap);
                break;
            case Type::ArrayType:
            {
                std::pmr::polymorphic_allocator<Array> alloc(
                    static_cast<Array *>(m_data.heap)->m_resource);
                auto *p = static_cast<Array *>(m_data.heap);
                std::destroy_at(p);
                alloc.deallocate(p, 1);
                break;
            }
            case Type::ObjectType:
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
            m_type = Type::Null;
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
        constexpr Json() = default;

        /**
         * @brief Construct null value
         */
        constexpr Json(std::nullptr_t) {}

        /**
         * @brief Construct bool value
         * @param val Boolean to store
         */
        constexpr Json(bool val) : m_type(Type::Boolean) { m_data.boolean = val; }

        /**
         * @brief Construct int64 value
         * @param val Integer to store
         */
        constexpr Json(int64_t val) : m_type(Type::Integer) { m_data.integer = val; }

        /**
         * @brief Construct double value
         * @param val Floating-point to store
         */
        constexpr Json(double val) : m_type(Type::Floating) { m_data.floating = val; }

        /**
         * @brief Construct int64 from any integer type (except bool)
         * @tparam T Integral type
         * @param val Value to store as int64
         */
        template <std::integral T>
            requires(!std::same_as<T, bool>)
        constexpr Json(T val) : m_type(Type::Integer), m_data{.integer = static_cast<int64_t>(val)}
        {
        }

        /**
         * @brief Construct double from any floating-point type
         * @tparam T Floating-point type
         * @param val Value to store as double
         */
        template <std::floating_point T>
        constexpr Json(T val) : m_type(Type::Floating), m_data{.floating = static_cast<double>(val)} {}

        /**
         * @brief Construct string from string_view (borrowed)
         * @param sv Source view — caller must guarantee lifetime
         * @note Does NOT copy. Stores {ptr, len} inline.
         */
        constexpr Json(std::string_view sv) : m_type(Type::StringView)
        {
            m_data.str_view.data = sv.data();
            m_data.str_view.length = static_cast<uint32_t>(sv.size());
        }

        /**
         * @brief Construct string from C string (borrowed view)
         * @param str NUL-terminated source — caller must guarantee lifetime
         * @note Does NOT copy. Internally wraps str in string_view.
         */
        constexpr Json(const char *str) : Json(std::string_view(str)) {}

        /**
         * @brief Construct string from string_view (owned: content copied into res)
         * @param sv Source view — content is copied; the source may die
         *        afterwards (unlike Json(std::string_view), which borrows)
         * @param res Resource for the string's heap buffer. Required
         *        (no default: a defaulted parameter would make every 1-arg
         *        Json(std::string_view) call ambiguous). nullptr falls back
         *        to the global config resource. Must outlive this Json —
         *        the buffer deallocates back into res on destruction.
         * @note The pmr::string object lives on global new/delete; only
         *       its buffer goes through res (same contract as String::own).
         *       No shared ownership: move transfers the buffer; deep-copy
         *       with clone(res). To adopt an already-materialised String
         *       (zero-copy) use the 1-arg Json(String&&) constructor.
         */
        Json(std::string_view sv, std::pmr::memory_resource *res)
            : m_type(Type::StringOwned)
        {
            if (!res)
                res = Config::instance().resource();
            m_data.heap = new std::pmr::string(sv, res);
        }

        /**
         * @brief Construct array value (takes ownership, heap-allocated)
         * @param arr Array to move into this value
         */
        Json(Array arr) : m_type(Type::ArrayType)
        {
            m_data.heap = heap_alloc(arr.m_resource, std::move(arr));
        }

        /**
         * @brief Construct object value (takes ownership, heap-allocated)
         * @param obj Object to move into this value
         */
        Json(Object obj) : m_type(Type::ObjectType)
        {
            m_data.heap = heap_alloc(obj.m_resource, std::move(obj));
        }

        /**
         * @brief Construct from String rvalue (used by parser).
         *
         * Borrowed strings store {ptr, len} inline as StringView.
         * Owned strings transfer the heap pointer via release().
         *
         * @param s String to adopt (must be an rvalue)
         */
        Json(String &&s) : m_type(Type::StringView)
        {
            std::string_view sv = s;
            m_data.str_view.data = sv.data();
            m_data.str_view.length = static_cast<uint32_t>(sv.size());

            if (s.is_owned())
            {
                m_type = Type::StringOwned;
                m_data.heap = s.release();
            }
        }

    public:
        /**
         * @brief Copy not allowed — use clone()
         */
        Json(const Json &) = delete;
        /**
         * @brief Copy not allowed — use clone()
         */
        Json &operator=(const Json &) = delete;

        /**
         * @brief Move construct (steals tagged data, marks source null)
         */
        constexpr Json(Json &&other) noexcept : m_type(other.m_type)
        {
            m_data = other.m_data;
            other.m_type = Type::Null;
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
                other.m_type = Type::Null;
            }
            return *this;
        }

        /**
         * @brief Destructor — frees heap-allocated types
         */
        constexpr ~Json()
        {
            if (!std::is_constant_evaluated())
                destroy();
        }

        /**
         * @brief Deep copy into specified memory resource
         * @param into Allocator for copied values (default: global config resource)
         * @return Independent deep copy
         * @note Strings are materialised (owned) in the target resource.
         */
        [[nodiscard]] Json clone(
            std::pmr::memory_resource *into = Config::instance().resource()) const;

        /**
         * @brief Construct an owned string value (static factory)
         * @param sv Source view — content is copied; the source may die
         *        afterwards
         * @param res Resource for the string's heap buffer (default:
         *        global config resource). Must outlive the returned Json.
         * @note Factory: returns a Json. Not an in-place operation
         *       (contrast String::own, which materialises *this).
         *       Convenience spelling of Json(sv, res) for the common
         *       global-resource case: Json::own("...") / Json::own(sv).
         */
        [[nodiscard]] static Json own(
            std::string_view sv,
            std::pmr::memory_resource *res = Config::instance().resource())
        {
            return Json(sv, res);
        }

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
            m_type = Type::Boolean;
            m_data.boolean = val;
            return *this;
        }

        /**
         * @brief Assign from String rvalue.
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
            m_data.str_view.data = sv.data();
            m_data.str_view.length = static_cast<uint32_t>(sv.size());
            if (s.is_owned())
            {
                m_type = Type::StringOwned;
                m_data.heap = s.release();
            }
            else
                m_type = Type::StringView;
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
            m_type = Type::Integer;
            m_data.integer = static_cast<int64_t>(val);
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
            m_type = Type::Floating;
            m_data.floating = static_cast<double>(val);
            return *this;
        }

        /**
         * @brief Assign string_view (borrowed)
         * @param val Source view — caller must guarantee lifetime
         * @return *this
         * @note Does NOT copy.
         */
        Json &operator=(std::string_view val);

        /**
         * @brief Assign C string (borrowed view)
         * @param val NUL-terminated source — caller must guarantee lifetime
         * @return *this
         * @note Does NOT copy. Internally wraps val in string_view.
         */
        Json &operator=(const char *val);

        /**
         * @brief Assign array (takes ownership, heap-allocated)
         * @param arr Array to move
         * @return *this
         */
        Json &operator=(Array &&arr);

        /**
         * @brief Assign object (takes ownership, heap-allocated)
         * @param obj Object to move
         * @return *this
         */
        Json &operator=(Object &&obj);

    public:
        /** @name Type checks */
        /**@{*/

        /**
         * @brief true if holds null
         */
        [[nodiscard]] constexpr bool is_null() const noexcept { return m_type == Type::Null; }

        /**
         * @brief true if holds bool
         */
        [[nodiscard]] constexpr bool is_boolean() const noexcept { return m_type == Type::Boolean; }

        /**
         * @brief true if holds int64
         */
        [[nodiscard]] constexpr bool is_int() const noexcept { return m_type == Type::Integer; }

        /**
         * @brief true if holds int64
         * @note Full-spelling alias for is_int() (the house name); both
         *       test the stored slot only — a 5.0 held in the Floating
         *       slot is NOT integer (type-based, not value-based).
         */
        [[nodiscard]] constexpr bool is_integer() const noexcept { return is_int(); }

        /**
         * @brief true if holds double
         */
        [[nodiscard]] constexpr bool is_float() const noexcept { return m_type == Type::Floating; }

        /**
         * @brief true if holds a number (int or float)
         */
        [[nodiscard]] constexpr bool is_number() const noexcept { return is_int() || is_float(); }

        /**
         * @brief true if holds a string (borrowed or owned)
         */
        [[nodiscard]] constexpr bool is_string() const noexcept { return m_type == Type::StringView || m_type == Type::StringOwned; }

        /**
         * @brief true if holds Array
         */
        [[nodiscard]] constexpr bool is_array() const noexcept { return m_type == Type::ArrayType; }

        /**
         * @brief true if holds Object
         */
        [[nodiscard]] constexpr bool is_object() const noexcept { return m_type == Type::ObjectType; }
        /**@}*/

    public:
        /** @name Unchecked access (no type validation) */
        /**@{*/

        /**
         * @brief Get null value
         */
        [[nodiscard]] constexpr std::nullptr_t as_null() const noexcept { return nullptr; }

        /**
         * @brief Get bool reference
         */
        [[nodiscard]] bool &as_boolean() PJH_JSON_NOEXCEPT
        {
            debug_check_type(m_type, Type::Boolean, "boolean");
            return m_data.boolean;
        }
        /**
         * @brief Get const bool reference
         */
        [[nodiscard]] const bool &as_boolean() const PJH_JSON_NOEXCEPT
        {
            debug_check_type(m_type, Type::Boolean, "boolean");
            return m_data.boolean;
        }

        /**
         * @brief Get int64 reference
         */
        [[nodiscard]] int64_t &as_int() PJH_JSON_NOEXCEPT
        {
            debug_check_type(m_type, Type::Integer, "int");
            return m_data.integer;
        }
        /**
         * @brief Get const int64 reference
         */
        [[nodiscard]] const int64_t &as_int() const PJH_JSON_NOEXCEPT
        {
            debug_check_type(m_type, Type::Integer, "int");
            return m_data.integer;
        }

        /**
         * @brief Get double reference
         */
        [[nodiscard]] double &as_float() PJH_JSON_NOEXCEPT
        {
            debug_check_type(m_type, Type::Floating, "float");
            return m_data.floating;
        }
        /**
         * @brief Get const double reference
         */
        [[nodiscard]] const double &as_float() const PJH_JSON_NOEXCEPT
        {
            debug_check_type(m_type, Type::Floating, "float");
            return m_data.floating;
        }

        /**
         * @brief Get string view.
         *
         * For StringView, returns a view of the inline {ptr, len} pair.
         * For StringOwned, returns a view of the heap-allocated pmr::string.
         *
         * @return View of the string content
         */
        [[nodiscard]] std::string_view as_string() const PJH_JSON_NOEXCEPT
        {
            debug_check_type2(m_type, Type::StringView, Type::StringOwned, "string");
            if (m_type == Type::StringView)
                return std::string_view(m_data.str_view.data, m_data.str_view.length);
            return *static_cast<std::pmr::string *>(m_data.heap);
        }

        /**
         * @brief Get array reference
         */
        [[nodiscard]] Array &as_array() PJH_JSON_NOEXCEPT
        {
            debug_check_type(m_type, Type::ArrayType, "array");
            return *static_cast<Array *>(m_data.heap);
        }
        /**
         * @brief Get const array reference
         */
        [[nodiscard]] const Array &as_array() const PJH_JSON_NOEXCEPT
        {
            debug_check_type(m_type, Type::ArrayType, "array");
            return *static_cast<const Array *>(m_data.heap);
        }

        /**
         * @brief Get object reference
         */
        [[nodiscard]] Object &as_object() PJH_JSON_NOEXCEPT
        {
            debug_check_type(m_type, Type::ObjectType, "object");
            return *static_cast<Object *>(m_data.heap);
        }
        /**
         * @brief Get const object reference
         */
        [[nodiscard]] const Object &as_object() const PJH_JSON_NOEXCEPT
        {
            debug_check_type(m_type, Type::ObjectType, "object");
            return *static_cast<const Object *>(m_data.heap);
        }
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
         * @name Checked numeric conversion (get / try_get)
         *
         * Fourth accessor family, deliberately OUTSIDE the as_* dual
         * track: it returns a computed conversion by value, so the
         * type guard stays active in BOTH build modes (contrast as_*:
         * release no-op; contrast at(): checked in both modes — get<T>
         * joins at()'s family). Tag is the single source of truth: a
         * slot is read only after m_type confirms it (no
         * non-active union member read in any mode).
         */
        /**@{*/
        /**
         * @brief Get the held numeric value as T (checked conversion)
         *
         * Accepted source slots:
         * - bool:      Boolean (identity)
         * - int64_t:   Integer only — a Floating source is rejected
         *               (narrowing: truncation/overflow, and the
         *               double->int64 cast of inf/NaN is UB)
         * - float:     Integer, Floating (defined rounding)
         * - double:    Integer, Floating (defined rounding)
         * Any other slot throws in debug AND release.
         *
         * @tparam T bool, int64_t, float or double (the slot's own
         *         types; other T is a compile error)
         * @return The value converted to T
         * @throws TypeError if the active slot is not in T's accepted set
         * @note Widening is defined rounding to the nearest
         *       representable value (round to even): values above 2^53
         *       may not round-trip (2^53+1 -> 2^53). Use is_int()/
         *       as_int() when int64 identity matters. Not an as_*
         *       dual-track accessor: conversions must compute in both
         *       modes, so the guard cannot be PJH_JSON_NOEXCEPT
         *       (same family as at(): checked in both modes).
         */
        template <typename T>
            requires(std::same_as<T, bool> || std::same_as<T, int64_t> ||
                     std::same_as<T, float> || std::same_as<T, double>)
        [[nodiscard]] T get() const
        {
            if constexpr (std::same_as<T, bool>)
            {
                if (m_type != Type::Boolean)
                    throw TypeError("type mismatch in get()");
                return m_data.boolean;
            }
            else if constexpr (std::same_as<T, int64_t>)
            {
                if (m_type != Type::Integer)
                    throw TypeError("type mismatch in get()");
                return m_data.integer;
            }
            else if constexpr (std::same_as<T, float>)
            {
                if (m_type == Type::Integer)
                    return static_cast<float>(m_data.integer);
                if (m_type == Type::Floating)
                    return static_cast<float>(m_data.floating);
                throw TypeError("type mismatch in get()");
            }
            else if constexpr (std::same_as<T, double>)
            {
                if (m_type == Type::Integer)
                    return static_cast<double>(m_data.integer);
                if (m_type == Type::Floating)
                    return m_data.floating;
                throw TypeError("type mismatch in get()");
            }
            else
            {
                static_assert(sizeof(T) == 0,
                              "get<T>: T must be bool, int64_t, float or double");
            }
        }

        /**
         * @brief Try get<T>() without throwing
         * @tparam T bool, int64_t, float or double
         * @return The value converted to T, or nullopt if the active
         *         slot is not in T's accepted set. Numeric widening
         *         always succeeds: try_get<double>() on an Integer
         *         yields the widened (nearest representable) value.
         */
        template <typename T>
            requires(std::same_as<T, bool> || std::same_as<T, int64_t> ||
                     std::same_as<T, float> || std::same_as<T, double>)
        [[nodiscard]] std::optional<T> try_get() const noexcept
        {
            if constexpr (std::same_as<T, bool>)
            {
                if (m_type == Type::Boolean)
                    return m_data.boolean;
            }
            else if constexpr (std::same_as<T, int64_t>)
            {
                if (m_type == Type::Integer)
                    return m_data.integer;
            }
            else if constexpr (std::same_as<T, float>)
            {
                if (m_type == Type::Integer)
                    return static_cast<float>(m_data.integer);
                if (m_type == Type::Floating)
                    return static_cast<float>(m_data.floating);
            }
            else if constexpr (std::same_as<T, double>)
            {
                if (m_type == Type::Integer)
                    return static_cast<double>(m_data.integer);
                if (m_type == Type::Floating)
                    return m_data.floating;
            }
            else
            {
                static_assert(sizeof(T) == 0,
                              "try_get<T>: T must be bool, int64_t, float or double");
            }
            return std::nullopt;
        }
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
         * @note The key is borrowed on insert; for keys that must outlive
         *       their source use Object::insert(key, val, res).
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
        /** @name Path access */
        /**@{*/
        /**
         * @brief Checked walk by path string (see parse_path for the grammar)
         * @param path Dotted/bracket path, e.g. "a.b[3].c"; empty = the root
         * @return Reference to the node the path resolves to
         * @throws std::invalid_argument if the path grammar is malformed
         * @throws TypeError if a hop's node is a scalar ("expected object"
         *         for a key hop, "expected array" for an index hop)
         * @throws std::out_of_range if a key is missing or an index is out
         *         of range (hop-local messages; no full-path context)
         * @note The parent node's runtime type decides the hop (RFC 6901):
         *       object parent resolves any step as a key (an index step uses
         *       its decimal string as the key name); array parent resolves
         *       any step as an index (a key step must be all-digit).
         * @note The empty path yields *this (the root itself).
         */
        [[nodiscard]] Json &at_path(std::string_view path);
        /**
         * @brief Const checked walk by path string
         * @param path Dotted/bracket path; empty = the root
         * @return Const reference to the node the path resolves to
         * @throws std::invalid_argument, TypeError, std::out_of_range — see at_path
         */
        [[nodiscard]] const Json &at_path(std::string_view path) const;
        /**
         * @brief Checked walk by typed path (unambiguous hop kinds)
         * @param path Sequence of key/index hops; empty = the root
         * @return Reference to the node the path resolves to
         * @throws TypeError, std::out_of_range — see at_path(std::string_view)
         * @note Same parent-decides rule as the string overload; the typed
         *       form is the escape hatch for keys containing '.', '[' or ']'
         */
        [[nodiscard]] Json &at_path(const Path &path);
        /**
         * @brief Const checked walk by typed path
         * @param path Sequence of key/index hops; empty = the root
         * @return Const reference to the node the path resolves to
         * @throws TypeError, std::out_of_range — see at_path(std::string_view)
         */
        [[nodiscard]] const Json &at_path(const Path &path) const;

        /**
         * @brief Safe walk: nullptr on any missing or wrong-type hop
         * @param path Dotted/bracket path; empty = the root
         * @return Pointer to the resolved node, or nullptr on a miss
         * @throws std::invalid_argument if the path grammar is malformed
         *         (a parameter error, not a miss — must not be swallowed)
         * @note try_as_* pointer convention: nullptr = miss (missing key,
         *       OOB index, or a scalar mid-walk hop).
         * @note The empty path yields this.
         */
        [[nodiscard]] Json *find_path(std::string_view path);
        /**
         * @brief Const safe walk by path string
         * @param path Dotted/bracket path; empty = the root
         * @return Const pointer to the resolved node, or nullptr on a miss
         * @throws std::invalid_argument if the path grammar is malformed
         */
        [[nodiscard]] const Json *find_path(std::string_view path) const;
        /**
         * @brief Safe walk by typed path
         * @param path Sequence of key/index hops; empty = the root
         * @return Pointer to the resolved node, or nullptr on a miss
         */
        [[nodiscard]] Json *find_path(const Path &path);
        /**
         * @brief Const safe walk by typed path
         * @param path Sequence of key/index hops; empty = the root
         * @return Const pointer to the resolved node, or nullptr on a miss
         */
        [[nodiscard]] const Json *find_path(const Path &path) const;

        /**
         * @brief Predicate: does the path resolve?
         * @param path Dotted/bracket path; empty = the root
         * @return true if every hop succeeds; a wrong-type (scalar mid-walk)
         *         hop is false, not an error
         * @throws std::invalid_argument if the path grammar is malformed
         * @note The empty path is always true. Keys containing '.', '[' or
         *       ']' need the typed overload (the DSL is lossy there).
         */
        [[nodiscard]] bool contains(std::string_view path) const;
        /**
         * @brief Predicate by typed path (no parsing, no throw)
         * @param path Sequence of key/index hops; empty = the root
         * @return true if every hop succeeds; a wrong-type (scalar mid-walk)
         *         hop is false, not an error
         */
        [[nodiscard]] bool contains(const Path &path) const noexcept;
        /**@}*/

    public:
        /** @name Comparison */
        /**@{*/
        /**
         * @brief Compare type and value with another Json
         * @param other Json to compare with
         * @return true if same type and value
         */
        [[nodiscard]] bool operator==(const Json &other) const;
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
        [[nodiscard]] bool operator==(const Array &val) const;
        /**
         * @brief Compare with Object
         * @return true if holds equal Object
         */
        [[nodiscard]] bool operator==(const Object &val) const;
        /**@}*/
    };

}

#endif // INCLUDE_PJH_JSON_JSON_HPP

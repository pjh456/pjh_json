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
#include <functional>
#include <variant>
#include <concepts>
#include <new>

#include "array.hpp"
#include "object.hpp"
#include "config.hpp"
#include "error.hpp"
#include "string.hpp"

namespace pjh::json
{

#ifdef NDEBUG
#define PJH_JSON_NOEXCEPT noexcept

    template <typename T>
    constexpr void debug_check_type(T, T, const char *) noexcept
    {
    }

    template <typename T>
    constexpr void debug_check_type2(T, T, T, const char *) noexcept
    {
    }

#else
#define PJH_JSON_NOEXCEPT

    template <typename T>
    inline void debug_check_type(T actual, T expected, const char *name)
    {
        if (actual != expected)
            throw TypeError(
                std::string("type mismatch in as_") + name + "()");
    }

    template <typename T>
    inline void debug_check_type2(T actual, T a, T b, const char *name)
    {
        if (actual != a && actual != b)
            throw TypeError(
                std::string("type mismatch in as_") + name + "()");
    }

#endif

    /**
     * @brief Both-modes type guard for the as_*_strict family
     *
     * Gateless twin of debug_check_type: throws TypeError in debug AND
     * release (as_*'s check compiles away under NDEBUG — a mismatched
     * call there reads an inactive union member, the UB class the
     * _strict family closes). Message keeps the house format
     * "type mismatch in as_<name>()"; the name argument carries the
     * "_strict" suffix so the message names the calling function.
     */
    template <typename T>
    inline void check_type_strict(T actual, T expected, const char *name)
    {
        if (actual != expected)
            throw TypeError(
                std::string("type mismatch in as_") + name + "()");
    }

    /**
     * @brief Both-modes two-tag variant (the string twins accept
     *        StringView and StringOwned)
     */
    template <typename T>
    inline void check_type_strict2(T actual, T a, T b, const char *name)
    {
        if (actual != a && actual != b)
            throw TypeError(
                std::string("type mismatch in as_") + name + "()");
    }

    // Forward declarations for the range-for iterator types. Defined after
    // class Json (their variant alternatives need the complete Json type).
    class JsonIterator;
    class ConstJsonIterator;

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
            size_t length;
        };

        // Borrowed lengths are size_t so >4 GiB strings are never truncated.
        static_assert(std::is_same_v<decltype(StrView::length), size_t>, "borrowed string length must be size_t");

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
         * 2. StringOwned: destroy the pmr::string header and its buffer
         *    through the resource that produced them (String::destroy_owned).
         * 3. ArrayType/ObjectType: destroy in place, then deallocate
         *    via PMR using the resource stored in the container.
         */
        void destroy()
        {
            switch (m_type)
            {
            case Type::StringOwned:
                String::destroy_owned(static_cast<std::pmr::string *>(m_data.heap));
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
         * @note A std::string temporary is rejected at compile time; use
         *       Json::own(sv, res) or Json(sv, res) to copy.
         */
        constexpr Json(std::string_view sv) : m_type(Type::StringView)
        {
            m_data.str_view.data = sv.data();
            m_data.str_view.length = sv.size();
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
         * @param res Resource for the string's object header and buffer.
         *        Required (no default: a defaulted parameter would make
         *        every 1-arg Json(std::string_view) call ambiguous). nullptr
         *        falls back to the global config resource. Must outlive this
         *        Json — both the header and the buffer deallocate back into
         *        res on destruction.
         * @note The pmr::string object header lives in res too (allocated via
         *       polymorphic_allocator<pmr::string>), not on the global heap
         *       (same contract as String::own).
         *       No shared ownership: move transfers the buffer; deep-copy
         *       with clone(res). To adopt an already-materialised String
         *       (zero-copy) use the 1-arg Json(String&&) constructor.
         */
        Json(std::string_view sv, std::pmr::memory_resource *res)
            : m_type(Type::StringOwned)
        {
            if (!res)
                res = Config::instance().resource();
            m_data.heap = String::make_owned(sv, res);
        }

        /**
         * @brief Deleted: borrowing a std::string temporary would dangle.
         * @note Use Json::own(sv, res) or Json(sv, res) to copy, or pass a
         *       view over a live buffer.
         */
        template <class T>
            requires is_string_rvalue_v<T>
        explicit Json(T &&) = delete;

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
         * Owned strings transfer the header pointer via release();
         * destruction frees it through String::destroy_owned.
         *
         * @param s String to adopt (must be an rvalue)
         */
        Json(String &&s) : m_type(Type::StringView)
        {
            std::string_view sv = s;
            m_data.str_view.data = sv.data();
            m_data.str_view.length = sv.size();

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
         * @param into Allocator for copied values (default: global config
         *        resource; nullptr also falls back to it)
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
         * transfer the header pointer (freed through
         * String::destroy_owned). Old value is destroyed first.
         *
         * @param s Source String (rvalue, ownership transferred)
         * @return *this
         */
        Json &operator=(String &&s)
        {
            destroy();
            std::string_view sv = s;
            m_data.str_view.data = sv.data();
            m_data.str_view.length = sv.size();
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
         * @note A std::string temporary is rejected at compile time.
         */
        Json &operator=(std::string_view val);

        /**
         * @brief Deleted: assigning a std::string temporary would dangle.
         * @note Use Json::own(sv, res) or Json(sv, res) to copy.
         */
        template <class T>
            requires is_string_rvalue_v<T>
        Json &operator=(T &&) = delete;

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
        /**
         * @name Unchecked access (no type validation)
         * @note Decision guide: README "Accessors" (track: known tag). A
         *       mismatched release call reads an inactive union member
         *       (undefined behavior) — see each member's @note; use
         *       as_*_strict()/try_as_*() when the tag is not guaranteed.
         */
        /**@{*/

        /**
         * @brief Get null value
         * @note Performs no type check in any build mode (returns nullptr for
         *       any held value) and reads no union member; use
         *       as_null_strict() for the checked form.
         */
        [[nodiscard]] constexpr std::nullptr_t as_null() const noexcept { return nullptr; }

        /**
         * @brief Get bool reference
         * @note Unchecked fast path: in release builds (NDEBUG) the type check
         *       is compiled away and a mismatched value reads an inactive union
         *       member (undefined behavior). Use as_boolean_strict() for a
         *       check that throws in both modes, or try_as_boolean() for the
         *       nullopt form.
         */
        [[nodiscard]] bool &as_boolean() PJH_JSON_NOEXCEPT
        {
            debug_check_type(m_type, Type::Boolean, "boolean");
            return m_data.boolean;
        }
        /**
         * @brief Get const bool reference
         * @note Unchecked like the non-const overload (release: UB on mismatch).
         */
        [[nodiscard]] const bool &as_boolean() const PJH_JSON_NOEXCEPT
        {
            debug_check_type(m_type, Type::Boolean, "boolean");
            return m_data.boolean;
        }

        /**
         * @brief Get int64 reference
         * @note Unchecked fast path: in release builds (NDEBUG) the type check
         *       is compiled away and a mismatched value reads an inactive union
         *       member (undefined behavior). Use as_int_strict() for a
         *       check that throws in both modes, or try_as_int() for the
         *       nullopt form.
         */
        [[nodiscard]] int64_t &as_int() PJH_JSON_NOEXCEPT
        {
            debug_check_type(m_type, Type::Integer, "int");
            return m_data.integer;
        }
        /**
         * @brief Get const int64 reference
         * @note Unchecked like the non-const overload (release: UB on mismatch).
         */
        [[nodiscard]] const int64_t &as_int() const PJH_JSON_NOEXCEPT
        {
            debug_check_type(m_type, Type::Integer, "int");
            return m_data.integer;
        }

        /**
         * @brief Get double reference
         * @note Unchecked fast path: in release builds (NDEBUG) the type check
         *       is compiled away and a mismatched value reads an inactive union
         *       member (undefined behavior). Use as_float_strict() for a
         *       check that throws in both modes, or try_as_float() for the
         *       nullopt form.
         */
        [[nodiscard]] double &as_float() PJH_JSON_NOEXCEPT
        {
            debug_check_type(m_type, Type::Floating, "float");
            return m_data.floating;
        }
        /**
         * @brief Get const double reference
         * @note Unchecked like the non-const overload (release: UB on mismatch).
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
         * @note Unchecked fast path: in release builds (NDEBUG) the type check
         *       is compiled away and a mismatched value reads an inactive union
         *       member (undefined behavior). Use as_string_strict() for a
         *       check that throws in both modes, or try_as_string() for the
         *       nullopt form.
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
         * @note Unchecked fast path: in release builds (NDEBUG) the type check
         *       is compiled away and a mismatched value reads an inactive union
         *       member (undefined behavior). Use as_array_strict() for a
         *       check that throws in both modes, or try_as_array() for the
         *       nullopt form.
         */
        [[nodiscard]] Array &as_array() PJH_JSON_NOEXCEPT
        {
            debug_check_type(m_type, Type::ArrayType, "array");
            return *static_cast<Array *>(m_data.heap);
        }
        /**
         * @brief Get const array reference
         * @note Unchecked like the non-const overload (release: UB on mismatch).
         */
        [[nodiscard]] const Array &as_array() const PJH_JSON_NOEXCEPT
        {
            debug_check_type(m_type, Type::ArrayType, "array");
            return *static_cast<const Array *>(m_data.heap);
        }

        /**
         * @brief Get object reference
         * @note Unchecked fast path: in release builds (NDEBUG) the type check
         *       is compiled away and a mismatched value reads an inactive union
         *       member (undefined behavior). Use as_object_strict() for a
         *       check that throws in both modes, or try_as_object() for the
         *       nullopt form.
         */
        [[nodiscard]] Object &as_object() PJH_JSON_NOEXCEPT
        {
            debug_check_type(m_type, Type::ObjectType, "object");
            return *static_cast<Object *>(m_data.heap);
        }
        /**
         * @brief Get const object reference
         * @note Unchecked like the non-const overload (release: UB on mismatch).
         */
        [[nodiscard]] const Object &as_object() const PJH_JSON_NOEXCEPT
        {
            debug_check_type(m_type, Type::ObjectType, "object");
            return *static_cast<const Object *>(m_data.heap);
        }
        /**@}*/

    public:
        /**
         * @name Strict access (type check throws in both build modes)
         *
         * Checked twins of as_*: identical return types; the type guard
         * is active in debug AND release (plain if + throw — no
         * PJH_JSON_NOEXCEPT, no #ifdef), throwing TypeError on mismatch.
         * The as_* fast path stays unchecked by design (in release,
         * you own the tag — see each as_* @note); this family is the
         * safe API for code that does not. For the nullopt/nullptr
         * form use try_as_* (already safe in both modes).
         *
         * @note Decision guide: README "Accessors" (track: checked
         *       reference read).
         */
        /**@{*/

        /**
         * @brief Checked null access
         * @throws TypeError if not null (debug AND release)
         * @note as_null() performs no type check in any build mode and
         *       reads no union member; this twin adds the check for
         *       family completeness (not an UB fix).
         */
        [[nodiscard]] std::nullptr_t as_null_strict() const
        {
            check_type_strict(m_type, Type::Null, "null_strict");
            return nullptr;
        }

        /**
         * @brief Checked bool reference (throws on mismatch, both modes)
         * @throws TypeError if not Boolean (debug AND release),
         *         "type mismatch in as_boolean_strict()"
         * @note Identical to as_boolean() except the guard never
         *       compiles away.
         */
        [[nodiscard]] bool &as_boolean_strict()
        {
            check_type_strict(m_type, Type::Boolean, "boolean_strict");
            return m_data.boolean;
        }
        /**
         * @brief Checked const bool reference
         * @throws TypeError if not Boolean (debug AND release)
         */
        [[nodiscard]] const bool &as_boolean_strict() const
        {
            check_type_strict(m_type, Type::Boolean, "boolean_strict");
            return m_data.boolean;
        }

        /**
         * @brief Checked int64 reference (throws on mismatch, both modes)
         * @throws TypeError if not Integer (debug AND release),
         *         "type mismatch in as_int_strict()"
         * @note Identical to as_int() except the guard never compiles away.
         */
        [[nodiscard]] int64_t &as_int_strict()
        {
            check_type_strict(m_type, Type::Integer, "int_strict");
            return m_data.integer;
        }
        /**
         * @brief Checked const int64 reference
         * @throws TypeError if not Integer (debug AND release)
         */
        [[nodiscard]] const int64_t &as_int_strict() const
        {
            check_type_strict(m_type, Type::Integer, "int_strict");
            return m_data.integer;
        }

        /**
         * @brief Checked double reference (throws on mismatch, both modes)
         * @throws TypeError if not Floating (debug AND release),
         *         "type mismatch in as_float_strict()"
         * @note Identical to as_float() except the guard never compiles away.
         */
        [[nodiscard]] double &as_float_strict()
        {
            check_type_strict(m_type, Type::Floating, "float_strict");
            return m_data.floating;
        }
        /**
         * @brief Checked const double reference
         * @throws TypeError if not Floating (debug AND release)
         */
        [[nodiscard]] const double &as_float_strict() const
        {
            check_type_strict(m_type, Type::Floating, "float_strict");
            return m_data.floating;
        }

        /**
         * @brief Checked string view (throws on mismatch, both modes)
         *
         * For StringView, returns a view of the inline {ptr, len} pair;
         * for StringOwned, a view of the heap-allocated pmr::string
         * (same body as as_string(), both tags accepted).
         *
         * @return View of the string content
         * @throws TypeError if not a string (debug AND release),
         *         "type mismatch in as_string_strict()"
         * @note Identical to as_string() except the guard never compiles away.
         */
        [[nodiscard]] std::string_view as_string_strict() const
        {
            check_type_strict2(m_type, Type::StringView, Type::StringOwned,
                                "string_strict");
            if (m_type == Type::StringView)
                return std::string_view(m_data.str_view.data, m_data.str_view.length);
            return *static_cast<std::pmr::string *>(m_data.heap);
        }

        /**
         * @brief Checked array reference (throws on mismatch, both modes)
         * @throws TypeError if not ArrayType (debug AND release),
         *         "type mismatch in as_array_strict()"
         * @note Identical to as_array() except the guard never compiles away.
         */
        [[nodiscard]] Array &as_array_strict()
        {
            check_type_strict(m_type, Type::ArrayType, "array_strict");
            return *static_cast<Array *>(m_data.heap);
        }
        /**
         * @brief Checked const array reference
         * @throws TypeError if not ArrayType (debug AND release)
         */
        [[nodiscard]] const Array &as_array_strict() const
        {
            check_type_strict(m_type, Type::ArrayType, "array_strict");
            return *static_cast<const Array *>(m_data.heap);
        }

        /**
         * @brief Checked object reference (throws on mismatch, both modes)
         * @throws TypeError if not ObjectType (debug AND release),
         *         "type mismatch in as_object_strict()"
         * @note Identical to as_object() except the guard never compiles away.
         */
        [[nodiscard]] Object &as_object_strict()
        {
            check_type_strict(m_type, Type::ObjectType, "object_strict");
            return *static_cast<Object *>(m_data.heap);
        }
        /**
         * @brief Checked const object reference
         * @throws TypeError if not ObjectType (debug AND release)
         */
        [[nodiscard]] const Object &as_object_strict() const
        {
            check_type_strict(m_type, Type::ObjectType, "object_strict");
            return *static_cast<const Object *>(m_data.heap);
        }
        /**@}*/

    public:
        /**
         * @name Safe access (returns nullopt/nullptr on type mismatch)
         * @note Decision guide: README "Accessors" (track: no-throw probe).
         *       The scalar probes try_as_boolean()/try_as_int() are
         *       equivalent to try_get<bool>()/try_get<int64_t>() (same
         *       signature, same accepted slot); try_as_float() matches
         *       try_get<double>() on the Floating slot, while
         *       try_get<double>() also widens Integer. For a converted value
         *       or a float target use try_get<T>().
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
         *
         * @note Decision guide: README "Accessors" (tracks: numeric
         *       conversion). Identity reads that return a reference/mutable
         *       slot use as_*_strict() (Integer/Bool/Floating); get<T>() is
         *       the value form and additionally covers the int64->float and
         *       int64->double widening the identity tracks cannot express.
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
         * @note A std::string temporary key is rejected at compile time.
         */
        Json &operator[](std::string_view key);

        /**
         * @brief Deleted: a std::string temporary key would dangle.
         * @note Use Object::insert(key, val, res) to own the key.
         */
        template <class T>
            requires is_string_rvalue_v<T>
        Json &operator[](T &&) = delete;
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
         * @brief Predicate by typed path (no parsing)
         * @param path Sequence of key/index hops; empty = the root
         * @return true if every hop succeeds; a wrong-type (scalar mid-walk)
         *         hop is false, not an error
         * @note Not noexcept: a hop may allocate — an index step over an
         *       object parent forms its decimal key, and a missed hop
         *       constructs an exception message. An OOM inside either must
         *       propagate as std::bad_alloc, not std::terminate.
         */
        [[nodiscard]] bool contains(const Path &path) const;
        /**@}*/

    public:
        /** @name Iteration (range-for) */
        /**@{*/
        /**
         * @brief Iterator over the value's children (range-for support)
         *
         * | active tag | loop variable (auto &&e)                    |
         * |------------|---------------------------------------------|
         * | ArrayType  | EntryView: e.key = empty view, e.value = element |
         * | ObjectType | EntryView: e.key = entry key, e.value = entry value |
         *
         * @return Iterator at the first child (== end() on an empty container)
         * @throws TypeError if *this is a scalar (both build modes;
         *         the at()/operator[] family)
         * @note The key is read-only: patch values through e.value, never
         *       keys. For an array element e.key is always the empty view —
         *       test is_array()/is_object(), not e.key.empty(), to tell an
         *       element from an empty-string-keyed entry.
         * @note The loop variable is bound to a per-step prvalue view:
         *       use auto &&e, auto e, or const auto &e (a plain auto &e
         *       cannot bind the prvalue yield).
         * @note Iteration is range-for-only: the Json-level iterators
         *       carry no iterator_traits member types, so std::distance/
         *       advance, the standard algorithms and the std::ranges
         *       iterator algorithms do not accept them, and Json does not
         *       model std::ranges::range through this track either
         *       (std::ranges::begin requires input_or_output_iterator).
         *       For algorithms use the Array iterators/data() or the
         *       const Object track; see "### Iteration" in README.md.
         * @warning Iterators are invalidated by any storage-changing
         *          operation on the container (Array push_back/resize/erase/
         *          clear; Object insert/remove/clear) — the std::vector
         *          contract. Dereferencing end() is undefined.
         * @code
         * auto doc = parse_copy(R"({"nums":[1,2,3]})");
         * for (auto &&e : doc.root()["nums"])
         *     e.value = Json(e.value.as_int() * 2);
         * @endcode
         */
        [[nodiscard]] JsonIterator begin();
        /**
         * @brief Iterator past the last child — see begin()
         * @throws TypeError if *this is a scalar
         */
        [[nodiscard]] JsonIterator end();
        /**
         * @brief Const iterator over the value's children
         * @return ConstJsonIterator (loop variable: ConstEntryView,
         *         e.value = const Json &)
         * @throws TypeError if *this is a scalar
         */
        [[nodiscard]] ConstJsonIterator begin() const;
        /**
         * @brief Const iterator past the last child — see begin() const
         * @throws TypeError if *this is a scalar
         */
        [[nodiscard]] ConstJsonIterator end() const;

        /**
         * @brief The object's keys, in entry (insertion) order
         * @return Zero-allocation view (a single pointer into the entry
         *         vector)
         * @throws TypeError if *this is not an Object (both build modes)
         * @note A duplicate-key overwrite keeps the first occurrence's
         *       position (last-wins semantics).
         * @note size() on a scalar is 1 (cardinality); keys() does not
         *       follow that contract: a scalar has no children, it throws.
         */
        [[nodiscard]] KeysView keys() const;
        /**
         * @brief The object's values (mutable), in entry order
         * @return Zero-allocation view; the loop variable is a Json &
         * @throws TypeError if *this is not an Object
         */
        [[nodiscard]] ValuesView values();
        /**
         * @brief The object's values (read-only), in entry order
         * @return Zero-allocation view; the loop variable is a const Json &
         * @throws TypeError if *this is not an Object
         */
        [[nodiscard]] ConstValuesView values() const;
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
        /**
         * @brief Strict weak ordering: class rank, then payload
         * @param other Json to compare with
         * @return true if *this sorts before other
         * @note Class rank: Null < Boolean < Integer < Floating <
         *       String < Array < Object. The two string storage tags
         *       share one rank and are compared by CONTENT (the same
         *       cross-tag fast path as operator==: equal content in
         *       different storage modes compares by content, never by
         *       tag).
         * @note Payload: bool/int64 raw; double with NaN canonicalized
         *       to the maximum (NaN > every non-NaN double, NaN is
         *       incomparable with itself); string byte-lexicographic;
         *       array element-lexicographic (a shorter prefix sorts
         *       first); object in canonical key-sorted form (entries
         *       ordered by key, then value -- insertion order is
         *       irrelevant, the order Object::operator== establishes).
         * @note Invariant (contract objects, no duplicate keys):
         *       a == b <=> !(a < b) && !(b < a). NaN is the one
         *       exception: equality implies incomparability and a < b
         *       implies a != b, but NaN != NaN while neither is less
         *       than the other. Duplicate-key objects (Object(Vec) /
         *       data() only) are outside this contract.
         * @warning The object compare allocates a temporary sort buffer
         *          (two pointer vectors), so operator< is not noexcept.
         */
        [[nodiscard]] bool operator<(const Json &other) const;
        /**@}*/

    public:
        /** @name Visitor dispatch (8-way over the Type tags) */
        /**@{*/

        /**
         * @brief The 8-way tag dispatch as a std::variant of payload aliases.
         *
         * Dispatches on m_type over all 8 Type tags; the two string tags
         * (StringView / StringOwned) share the single std::string_view
         * alternative, mirroring is_string()/as_string(). Scalar and
         * container payloads are std::reference_wrapper<T> (a raw reference
         * is not a valid variant alternative — [variant.requirements]:
         * alternatives must be non-array object types).
         *
         * | m_type      | const payload                      | non-const payload                |
         * |-------------|------------------------------------|----------------------------------|
         * | Null        | std::monostate                     | std::monostate                   |
         * | Boolean     | reference_wrapper<const bool>     | reference_wrapper<bool>         |
         * | Integer     | reference_wrapper<const int64_t>  | reference_wrapper<int64_t>       |
         * | Floating    | reference_wrapper<const double>    | reference_wrapper<double>       |
         * | StringView  | std::string_view                   | std::string_view                |
         * | StringOwned | std::string_view                   | std::string_view                |
         * | ArrayType   | reference_wrapper<const Array>     | reference_wrapper<Array>        |
         * | ObjectType  | reference_wrapper<const Object>    | reference_wrapper<Object>       |
         *
         * @return A prvalue variant whose alternatives alias *this. The
         *         variant is copyable — a copy of the returned variant is
         *         another alias to the same storage (the wrapper
         *         alternatives copy the reference, not the referent), not
         *         a deep copy.
         * @warning Valid only while *this is alive and unmutated: any
         *          operator=, move, or destructor call on *this invalidates
         *          every alias. The string_view alternative additionally
         *          requires its source buffer to stay alive (same contract
         *          as as_string()).
         * @note Strict-by-construction: dispatch is on the tag itself, so
         *       only the active union member is ever read (no debug_check
         *       needed — there is nothing to check). The as_* name without
         *       a debug_check body is a deliberate track deviation (the
         *       exhaustive tag dispatch reads only the active member).
         */
        [[nodiscard]] std::variant<std::monostate,
                                   std::reference_wrapper<const bool>,
                                   std::reference_wrapper<const int64_t>,
                                   std::reference_wrapper<const double>,
                                   std::string_view,
                                   std::reference_wrapper<const Array>,
                                   std::reference_wrapper<const Object>>
        as_variant() const noexcept;

        /**
         * @brief Non-const as_variant — the referenced payloads are patchable
         *        in place (v.get() = ...); the tag itself can never change.
         *        See the const overload for the table and the lifetime
         *        contract.
         */
        [[nodiscard]] std::variant<std::monostate,
                                   std::reference_wrapper<bool>,
                                   std::reference_wrapper<int64_t>,
                                   std::reference_wrapper<double>,
                                   std::string_view,
                                   std::reference_wrapper<Array>,
                                   std::reference_wrapper<Object>>
        as_variant() noexcept;

        /**
         * @brief Visit *this with any callable invocable with each of the
         *        as_variant() payload types (the std::visit protocol: a
         *        generic lambda with if-constexpr, or an overload set).
         *
         * The payload passed to f is the variant's active alternative:
         * std::monostate, std::string_view (by value), or
         * std::reference_wrapper<T> — extract the referent with .get().
         *
         * @tparam F Visitor callable type
         * @param f Visitor (forwarded)
         * @return Whatever f returns (decltype(auto) propagation)
         * @note Equivalent to std::visit(std::forward<F>(f), as_variant()).
         *       Mutating the *referenced* payload through the non-const
         *       overload is the .get() = ... spelling only
         *       (reference_wrapper has no operator=(T&) — `v = x` does not
         *       compile; it is a safe failure, not a silent miss). The tag
         *       itself can never change. The if-constexpr wrapper spellings
         *       must match the overload's constness (const visit ->
         *       reference_wrapper<const T> spellings): a mismatched spelling
         *       compiles as a no-op, not an error. A 7-parameter non-generic
         *       lambda cannot work (lambdas have no overloads).
         * @code
         * std::string render(const Json &j)
         * {
         *     return j.visit([](auto &&v) -> std::string
         *     {
         *         using T = std::decay_t<decltype(v)>;
         *         if constexpr (std::same_as<T, std::monostate>)
         *             return "null";
         *         else if constexpr (std::same_as<T, std::reference_wrapper<const bool>>)
         *             return v.get() ? "true" : "false";
         *         else if constexpr (std::same_as<T, std::reference_wrapper<const int64_t>>)
         *             return std::to_string(v.get());
         *         else if constexpr (std::same_as<T, std::reference_wrapper<const double>>)
         *             return std::to_string(v.get());
         *         else if constexpr (std::same_as<T, std::string_view>)
         *             return std::string(v);
         *         else if constexpr (std::same_as<T, std::reference_wrapper<const Array>>)
         *             return "[" + std::to_string(v.get().size()) + "]";
         *         else
         *             return "{" + std::to_string(v.get().size()) + "}";
         *     });
         * }
         * @endcode
         */
        template <typename F>
        auto visit(F &&f) const
        {
            return std::visit(std::forward<F>(f), as_variant());
        }

        /**
         * @brief Non-const visit — see the const overload for the protocol.
         */
        template <typename F>
        auto visit(F &&f)
        {
            return std::visit(std::forward<F>(f), as_variant());
        }
        /**@}*/
    };

    /**
     * @brief One iteration step of a Json range-for (non-const track).
     *
     * key is a read-only view into the owning object's key String (the
     * empty view for an array element); value references the child Json
     * in place. A pure projection: owns no state of its own.
     */
    struct EntryView
    {
        std::string_view key;  ///< empty for an array element
        Json &value;            ///< the child, in place (patchable)
    };

    /**
     * @brief One iteration step of a Json range-for (const track).
     * Same shape as EntryView with a const value reference.
     */
    struct ConstEntryView
    {
        std::string_view key;  ///< empty for an array element
        const Json &value;     ///< the child, read-only
    };

    /**
     * @brief Iterator returned by Json::begin()/end() (non-const track).
     *
     * Wraps either the Array's vector iterator or the Object's entry
     * iterator (chosen at construction by Json::begin()); operator*
     * synthesizes an EntryView (key = the entry's key, or the empty view
     * for an array element; value = the child, in place).
     * Designed for range-for only: no iterator_traits typedefs;
     * std::distance/advance are not supported, and no standard or
     * std::ranges iterator algorithm accepts it. Pre-increment only and
     * no operator->. The variant of alternatives (the Object::iterator
     * alternative is non-copy-assignable) makes this iterator
     * non-copy-assignable too. Because std::ranges::begin constrains its
     * result on input_or_output_iterator, Json does not model
     * std::ranges::range through this track; for algorithms use the Array
     * native iterators/data() or the const Object track.
     */
    class JsonIterator
    {
    public:
        /**
         * @brief The child at the cursor (prvalue view into the container)
         */
        [[nodiscard]] EntryView operator*() const noexcept
        {
            return std::visit(
                [](auto &it) -> EntryView
                {
                    using It = std::decay_t<decltype(it)>;
                    if constexpr (std::same_as<It, Array::Vec::iterator>)
                        return EntryView{std::string_view{}, *it};
                    else
                        return EntryView{static_cast<std::string_view>(it->first),
                                          it->second};
                },
                m_it);
        }

        /**
         * @brief Advance one child (undefined past end)
         */
        JsonIterator &operator++() noexcept
        {
            std::visit([](auto &it) { ++it; }, m_it);
            return *this;
        }

        /**
         * @brief Iterator equality (same container and same position)
         */
        friend bool operator==(const JsonIterator &a, const JsonIterator &b) noexcept
        {
            return a.m_it == b.m_it;
        }
        friend bool operator!=(const JsonIterator &a, const JsonIterator &b) noexcept
        {
            return a.m_it != b.m_it;
        }

    private:
        friend class Json;
        explicit JsonIterator(Array::Vec::iterator it) : m_it(std::move(it)) {}
        explicit JsonIterator(Object::iterator it) : m_it(std::move(it)) {}

        std::variant<Array::Vec::iterator, Object::iterator> m_it;
    };

    /**
     * @brief Iterator returned by Json::begin() const / end() const.
     * Same mechanism as JsonIterator with a const value reference.
     * Range-for-only like JsonIterator: no iterator_traits; pre-increment
     * only; no operator->; const Json does not model std::ranges::range
     * through it. For algorithms use the underlying const container
     * iterators (Array / const Object).
     */
    class ConstJsonIterator
    {
    public:
        /**
         * @brief The child at the cursor (prvalue view into the container)
         */
        [[nodiscard]] ConstEntryView operator*() const noexcept
        {
            return std::visit(
                [](auto &it) -> ConstEntryView
                {
                    using It = std::decay_t<decltype(it)>;
                    if constexpr (std::same_as<It, Array::Vec::const_iterator>)
                        return ConstEntryView{std::string_view{}, *it};
                    else
                        return ConstEntryView{static_cast<std::string_view>(it->first),
                                               it->second};
                },
                m_it);
        }

        /**
         * @brief Advance one child (undefined past end)
         */
        ConstJsonIterator &operator++() noexcept
        {
            std::visit([](auto &it) { ++it; }, m_it);
            return *this;
        }

        /**
         * @brief Iterator equality (same container and same position)
         */
        friend bool operator==(const ConstJsonIterator &a, const ConstJsonIterator &b) noexcept
        {
            return a.m_it == b.m_it;
        }
        friend bool operator!=(const ConstJsonIterator &a, const ConstJsonIterator &b) noexcept
        {
            return a.m_it != b.m_it;
        }

    private:
        friend class Json;
        explicit ConstJsonIterator(Array::Vec::const_iterator it) : m_it(std::move(it)) {}
        explicit ConstJsonIterator(Object::Vec::const_iterator it) : m_it(std::move(it)) {}

        std::variant<Array::Vec::const_iterator, Object::Vec::const_iterator> m_it;
    };

    /**
     * @brief Zero-allocation view of an Object's keys (insertion order).
     *
     * Holds a single pointer into the Object's entry vector; keys are
     * yielded as std::string_view by value through the nested iterator.
     * Valid while the Object's storage stays stable (a move-assign of the
     * Object invalidates the view — the std::vector contract).
     *
     * The nested KeyIt is a range-for-only projection iterator: no
     * iterator_traits, no standard algorithm support (for that, use the
     * const Object iterators/data()).
     */
    class KeysView
    {
    public:
        /**
         * @brief Keys iterator: *it = a std::string_view into the key
         * @note Range-for-only: no iterator_traits, pre-increment only.
         */
        class KeyIt
        {
        public:
            [[nodiscard]] std::string_view operator*() const noexcept
            {
                return static_cast<std::string_view>(m->first);
            }
            KeyIt &operator++() noexcept { ++m; return *this; }
            friend bool operator==(const KeyIt &a, const KeyIt &b) noexcept { return a.m == b.m; }
            friend bool operator!=(const KeyIt &a, const KeyIt &b) noexcept { return a.m != b.m; }
        private:
            friend class KeysView;
            using VecIt = Object::Vec::const_iterator;
            explicit KeyIt(VecIt it) noexcept : m(it) {}
            VecIt m;
        };

        /**
         * @brief First key (== end() on an empty object)
         */
        [[nodiscard]] KeyIt begin() const noexcept { return KeyIt(m->begin()); }
        /**
         * @brief Past the last key
         */
        [[nodiscard]] KeyIt end() const noexcept { return KeyIt(m->end()); }

    private:
        friend class Object;
        explicit KeysView(const Object::Vec *v) noexcept : m(v) {}
        const Object::Vec *m{nullptr};
    };

    /**
     * @brief Zero-allocation view of an Object's values (mutable track).
     *
     * Same mechanism as KeysView; the nested iterator yields Json &
     * (patch in place). Order = entry order.
     *
     * The nested ValueIt is a range-for-only projection iterator: no
     * iterator_traits, no standard algorithm support.
     */
    class ValuesView
    {
    public:
        /**
         * @brief Values iterator: *it = Json & (the child, in place)
         * @note Range-for-only: no iterator_traits, pre-increment only.
         */
        class ValueIt
        {
        public:
            [[nodiscard]] Json &operator*() const noexcept { return m->second; }
            ValueIt &operator++() noexcept { ++m; return *this; }
            friend bool operator==(const ValueIt &a, const ValueIt &b) noexcept { return a.m == b.m; }
            friend bool operator!=(const ValueIt &a, const ValueIt &b) noexcept { return a.m != b.m; }
        private:
            friend class ValuesView;
            using VecIt = Object::Vec::iterator;
            explicit ValueIt(VecIt it) noexcept : m(it) {}
            VecIt m;
        };

        [[nodiscard]] ValueIt begin() noexcept { return ValueIt(m->begin()); }
        [[nodiscard]] ValueIt end() noexcept { return ValueIt(m->end()); }

    private:
        friend class Object;
        explicit ValuesView(Object::Vec *v) noexcept : m(v) {}
        Object::Vec *m{nullptr};
    };

    /**
     * @brief Zero-allocation view of an Object's values (const track).
     * Same as ValuesView yielding const Json &.
     *
     * The nested ValueIt is a range-for-only projection iterator: no
     * iterator_traits, no standard algorithm support.
     */
    class ConstValuesView
    {
    public:
        /**
         * @brief Const values iterator: *it = const Json &
         * @note Range-for-only: no iterator_traits, pre-increment only.
         */
        class ValueIt
        {
        public:
            [[nodiscard]] const Json &operator*() const noexcept { return m->second; }
            ValueIt &operator++() noexcept { ++m; return *this; }
            friend bool operator==(const ValueIt &a, const ValueIt &b) noexcept { return a.m == b.m; }
            friend bool operator!=(const ValueIt &a, const ValueIt &b) noexcept { return a.m != b.m; }
        private:
            friend class ConstValuesView;
            using VecIt = Object::Vec::const_iterator;
            explicit ValueIt(VecIt it) noexcept : m(it) {}
            VecIt m;
        };

        [[nodiscard]] ValueIt begin() const noexcept { return ValueIt(m->begin()); }
        [[nodiscard]] ValueIt end() const noexcept { return ValueIt(m->end()); }

    private:
        friend class Object;
        explicit ConstValuesView(const Object::Vec *v) noexcept : m(v) {}
        const Object::Vec *m{nullptr};
    };

    /// @cond DOXYGEN_SKIP
    template <class... Ts>
        requires(std::constructible_from<Json, Ts> && ...)
    inline Array Array::of(Ts &&...vals)
    {
        Array a;
        a.reserve(sizeof...(vals));
        (a.push_back(Json(std::forward<Ts>(vals))), ...);
        return a;
    }

    template <class... Es>
        requires(std::convertible_to<Es, Object::Entry> && ...)
    inline Object Object::of(Es &&...entries)
    {
        Object o;
        (o.insert(Object::Entry(std::forward<Es>(entries))), ...);
        return o;
    }
    /// @endcond
}

// std::hash<Json> — content-based (equal values hash equal; the payload
// recipe lives in src/json.cpp beside operator<'s payload compares).
// Declaration here; the single member definition is in src/json.cpp
// (the as_variant header-decl/src-def house shape).
template <>
struct std::hash<pjh::json::Json>
{
    size_t operator()(const pjh::json::Json &value) const;
};

#endif // INCLUDE_PJH_JSON_JSON_HPP

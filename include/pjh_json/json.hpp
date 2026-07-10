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
     * @brief Core JSON value type
     *
     * Variant holding one of: null, bool, int64, double, String, Array, Object.
     * Copy disabled -- use clone() for deep copy.
     *
     * @note String values constructed from string_view/const char* borrow the
     *       caller's buffer. Ensure source outlives this Json or call
     *       String::own() to materialise the copy.
     */
    class Json
    {

    public:
        /**
         * @brief Underlying variant type
         */
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
        /**
         * @brief Construct null value
         */
        Json() = default;

        /**
         * @brief Construct null value
         */
        Json(std::nullptr_t) : m_data(nullptr) {}

        /**
         * @brief Construct bool value
         * @param val Boolean to store
         */
        Json(bool val) : m_data(val) {}

        /**
         * @brief Construct int64 from any integer type (except bool)
         * @tparam T Integral type
         * @param val Value to store as int64
         */
        template <std::integral T>
            requires(!std::same_as<T, bool>)
        Json(T val) : m_data(static_cast<int64_t>(val))
        {
        }

        /**
         * @brief Construct double from any floating-point type
         * @tparam T Floating-point type
         * @param val Value to store as double
         */
        template <std::floating_point T>
        Json(T val) : m_data(static_cast<double>(val)) {}

        /**
         * @brief Construct string from string_view (borrowed)
         * @param str Source view -- caller must guarantee lifetime
         * @note Does NOT copy. The Json borrows the string_view.
         */
        Json(std::string_view str) : m_data(str) {}

        /**
         * @brief Construct string from C string (borrowed view)
         * @param str NUL-terminated source -- caller must guarantee lifetime
         * @note Does NOT copy. Internally wraps str in string_view.
         */
        Json(const char *str) : m_data(std::string_view(str)) {}

        /**
         * @brief Construct array value (takes ownership)
         * @param arr Array to move into this value
         */
        Json(Array arr) : m_data(std::move(arr)) {}

        /**
         * @brief Construct object value (takes ownership)
         * @param obj Object to move into this value
         */
        Json(Object obj) : m_data(std::move(obj)) {}

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
         * @brief Move construct (default)
         */
        Json(Json &&) noexcept = default;
        /**
         * @brief Move assign (default)
         */
        Json &operator=(Json &&) noexcept = default;

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
        Json &operator=(std::nullptr_t);

        /**
         * @brief Assign bool
         * @param val Boolean to assign
         * @return *this
         */
        Json &operator=(bool val);

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
            m_data = static_cast<int64_t>(val);
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
            m_data = static_cast<double>(val);
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
         * @note Does NOT copy.
         */
        Json &operator=(const char *val);

        /**
         * @brief Assign array (takes ownership)
         * @param arr Array to move
         * @return *this
         */
        Json &operator=(Array &&arr) noexcept;

        /**
         * @brief Assign object (takes ownership)
         * @param obj Object to move
         * @return *this
         */
        Json &operator=(Object &&obj) noexcept;

    public:
        /** @name Type checks */
        /**@{*/
        /**
         * @brief true if holds null
         * @return Whether variant holds nullptr_t
         */
        [[nodiscard]] bool is_null() const noexcept { return std::holds_alternative<std::nullptr_t>(m_data); }
        /**
         * @brief true if holds bool
         * @return Whether variant holds bool
         */
        [[nodiscard]] bool is_boolean() const noexcept { return std::holds_alternative<bool>(m_data); }
        /**
         * @brief true if holds int64
         * @return Whether variant holds int64_t
         */
        [[nodiscard]] bool is_int() const noexcept { return std::holds_alternative<std::int64_t>(m_data); }
        /**
         * @brief true if holds double
         * @return Whether variant holds double
         */
        [[nodiscard]] bool is_float() const noexcept { return std::holds_alternative<double>(m_data); }
        /**
         * @brief true if variant is a number (int or float)
         * @return is_int() || is_float()
         */
        [[nodiscard]] bool is_number() const noexcept { return is_int() || is_float(); }
        /**
         * @brief true if holds String
         * @return Whether variant holds String
         */
        [[nodiscard]] bool is_string() const noexcept { return std::holds_alternative<String>(m_data); }
        /**
         * @brief true if holds Array
         * @return Whether variant holds Array
         */
        [[nodiscard]] bool is_array() const noexcept { return std::holds_alternative<Array>(m_data); }
        /**
         * @brief true if holds Object
         * @return Whether variant holds Object
         */
        [[nodiscard]] bool is_object() const noexcept { return std::holds_alternative<Object>(m_data); }
        /**@}*/

    public:
        /**
         * @name Unchecked access
         * @throws std::bad_variant_access on wrong type
         */
        /**@{*/
        /**
         * @brief Get null value
         * @throws std::bad_variant_access if not null
         */
        [[nodiscard]] std::nullptr_t as_null() { return std::get<std::nullptr_t>(m_data); }

        /**
         * @brief Get bool reference
         * @throws std::bad_variant_access if not bool
         */
        [[nodiscard]] bool &as_boolean() { return std::get<bool>(m_data); }
        /**
         * @brief Get const bool reference
         * @throws std::bad_variant_access if not bool
         */
        [[nodiscard]] const bool &as_boolean() const { return std::get<bool>(m_data); }

        /**
         * @brief Get int64 reference
         * @throws std::bad_variant_access if not int64
         */
        [[nodiscard]] std::int64_t &as_int() { return std::get<std::int64_t>(m_data); }
        /**
         * @brief Get const int64 reference
         * @throws std::bad_variant_access if not int64
         */
        [[nodiscard]] const std::int64_t &as_int() const { return std::get<std::int64_t>(m_data); }

        /**
         * @brief Get double reference
         * @throws std::bad_variant_access if not double
         */
        [[nodiscard]] double &as_float() { return std::get<double>(m_data); }
        /**
         * @brief Get const double reference
         * @throws std::bad_variant_access if not double
         */
        [[nodiscard]] const double &as_float() const { return std::get<double>(m_data); }

        /**
         * @brief Get string view
         * @throws std::bad_variant_access if not String
         */
        [[nodiscard]] std::string_view as_string() { return std::get<String>(m_data); }
        /**
         * @brief Get const string view
         * @throws std::bad_variant_access if not String
         */
        [[nodiscard]] std::string_view as_string() const { return std::get<String>(m_data); }

        /**
         * @brief Get array reference
         * @throws std::bad_variant_access if not Array
         */
        [[nodiscard]] Array &as_array() { return std::get<Array>(m_data); }
        /**
         * @brief Get const array reference
         * @throws std::bad_variant_access if not Array
         */
        [[nodiscard]] const Array &as_array() const { return std::get<Array>(m_data); }

        /**
         * @brief Get object reference
         * @throws std::bad_variant_access if not Object
         */
        [[nodiscard]] Object &as_object() { return std::get<Object>(m_data); }
        /**
         * @brief Get const object reference
         * @throws std::bad_variant_access if not Object
         */
        [[nodiscard]] const Object &as_object() const { return std::get<Object>(m_data); }
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
        [[nodiscard]] std::optional<std::int64_t> try_as_int() const noexcept;
        /**
         * @brief Try get double
         * @return nullopt if not double
         */
        [[nodiscard]] std::optional<double> try_as_float() const noexcept;
        /**
         * @brief Try get string view
         * @return nullopt if not String
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
        /**
         * @name Array element access
         * @throws TypeError if not an array
         */
        /**@{*/
        /**
         * @brief Index access (no bounds check)
         * @param idx Element index
         * @throws TypeError if variant is not Array
         */
        Json &operator[](size_t idx);
        /**
         * @brief Const index access (no bounds check)
         * @param idx Element index
         * @throws TypeError if variant is not Array
         */
        const Json &operator[](size_t idx) const;
        /**
         * @brief Index access with bounds check
         * @param idx Element index
         * @throws TypeError if variant is not Array
         * @throws std::out_of_range if idx >= size()
         */
        Json &at(size_t idx);
        /**
         * @brief Const index access with bounds check
         * @param idx Element index
         * @throws TypeError if variant is not Array
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
         * @throws TypeError if variant is not Object
         * @note Missing key is default-constructed in place.
         */
        Json &operator[](std::string_view key);
        /**
         * @brief Const access key
         * @param key Field name
         * @throws TypeError if variant is not Object
         * @throws std::out_of_range if key not found
         */
        const Json &operator[](std::string_view key) const;
        /**
         * @brief Access key with bounds check
         * @param key Field name
         * @throws TypeError if variant is not Object
         * @throws std::out_of_range if key not found
         */
        Json &at(std::string_view key);
        /**
         * @brief Const access key with bounds check
         * @param key Field name
         * @throws TypeError if variant is not Object
         * @throws std::out_of_range if key not found
         */
        const Json &at(std::string_view key) const;
        /**@}*/

    public:
        /** @name Comparison */
        /**@{*/
        /**
         * @brief Compare variant content directly
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

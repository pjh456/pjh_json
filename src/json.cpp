#include "pjh_json/json.hpp"

namespace pjh::json
{
    // --- operator= ---

    /*
     * Assign string_view (borrowed)
     *
     * 1. Destroy old value.
     * 2. Store {ptr, len} inline as StringView.
     */
    Json &Json::operator=(std::string_view val)
    {
        destroy();
        m_type = Type::StringView;
        m_data.str_view.data = val.data();
        m_data.str_view.length = static_cast<uint32_t>(val.size());
        return *this;
    }

    /*
     * Assign C string (delegates to string_view overload)
     */
    Json &Json::operator=(const char *val)
    {
        return operator=(std::string_view(val));
    }

    /*
     * Assign array (takes ownership, heap-allocated via PMR)
     *
     * 1. Destroy old value.
     * 2. Allocate and construct Array on heap using its own m_resource.
     * 3. Store pointer as ArrayType.
     */
    Json &Json::operator=(Array &&arr)
    {
        destroy();
        m_data.heap = heap_alloc(arr.m_resource, std::move(arr));
        m_type = Type::ArrayType;
        return *this;
    }

    /*
     * Assign object (takes ownership, heap-allocated via PMR)
     *
     * 1. Destroy old value.
     * 2. Allocate and construct Object on heap using its own m_resource.
     * 3. Store pointer as ObjectType.
     */
    Json &Json::operator=(Object &&obj)
    {
        destroy();
        m_data.heap = heap_alloc(obj.m_resource, std::move(obj));
        m_type = Type::ObjectType;
        return *this;
    }

    // --- as_variant ---

    /*
     * Visitor dispatch: 8-way switch over m_type into a variant of
     * std::reference_wrapper payload aliases.
     *
     * 1. Raw references are not valid variant alternatives
     *    ([variant.requirements]: non-array object types only); the
     *    reference_wrapper<T> spellings carry the same aliasing semantics
     *    (extract the referent with .get()).
     * 2. StringView and StringOwned both yield std::string_view (the
     *    user-facing "string" is one type, same as as_string()). The two
     *    case labels stay stacked — 8-way = every tag covered, not
     *    case-label count (clone() shape, :83-84).
     * 3. The alternatives alias *this — the returned variant is valid only
     *    while *this is alive and unmutated (doxygen @warning).
     * 4. Exhaustive case list; the trailing return is unreachable
     *    (clone() idiom, :111).
     */
    std::variant<std::monostate,
                 std::reference_wrapper<const bool>,
                 std::reference_wrapper<const int64_t>,
                 std::reference_wrapper<const double>,
                 std::string_view,
                 std::reference_wrapper<const Array>,
                 std::reference_wrapper<const Object>>
    Json::as_variant() const noexcept
    {
        switch (m_type)
        {
        case Type::Null:
            return std::monostate{};
        case Type::Boolean:
            return m_data.boolean;
        case Type::Integer:
            return m_data.integer;
        case Type::Floating:
            return m_data.floating;
        case Type::StringView:
        case Type::StringOwned:
            return as_string();
        case Type::ArrayType:
            return *static_cast<const Array *>(m_data.heap);
        case Type::ObjectType:
            return *static_cast<const Object *>(m_data.heap);
        }
        return std::monostate{};
    }

    std::variant<std::monostate,
                 std::reference_wrapper<bool>,
                 std::reference_wrapper<int64_t>,
                 std::reference_wrapper<double>,
                 std::string_view,
                 std::reference_wrapper<Array>,
                 std::reference_wrapper<Object>>
    Json::as_variant() noexcept
    {
        switch (m_type)
        {
        case Type::Null:
            return std::monostate{};
        case Type::Boolean:
            return m_data.boolean;
        case Type::Integer:
            return m_data.integer;
        case Type::Floating:
            return m_data.floating;
        case Type::StringView:
        case Type::StringOwned:
            return as_string();
        case Type::ArrayType:
            return *static_cast<Array *>(m_data.heap);
        case Type::ObjectType:
            return *static_cast<Object *>(m_data.heap);
        }
        return std::monostate{};
    }

    // --- clone ---

    /*
     * Deep copy into target memory resource
     *
     * 1. Scalars (null/boolean/integer/floating): copy by value.
     * 2. String (borrowed or owned): read string_view, allocate owned
     *    pmr::string copy in target resource, store as StringOwned.
     * 3. Array/Object: delegate to container's clone() which recursively
     *    clones all children, then heap-allocate via PMR.
     */
    Json Json::clone(std::pmr::memory_resource *into) const
    {
        switch (m_type)
        {
        case Type::Null:
            return nullptr;
        case Type::Boolean:
            return m_data.boolean;
        case Type::Integer:
            return m_data.integer;
        case Type::Floating:
            return m_data.floating;
        case Type::StringView:
        case Type::StringOwned:
        {
            std::string_view sv = as_string();
            Json out;
            out.m_type = Type::StringOwned;
            out.m_data.heap = String::make_owned(sv, into);
            return out;
        }
        case Type::ArrayType:
        {
            auto *ptr = heap_alloc(into, as_array().clone(into));
            Json out;
            out.m_type = Type::ArrayType;
            out.m_data.heap = ptr;
            return out;
        }
        case Type::ObjectType:
        {
            auto *ptr = heap_alloc(into, as_object().clone(into));
            Json out;
            out.m_type = Type::ObjectType;
            out.m_data.heap = ptr;
            return out;
        }
        }
        return nullptr;
    }

    // --- try_as ---

    /*
     * Safe access: check internal type, return nullopt/nullptr on mismatch.
     *
     * 1. Scalar types: compare m_type, return the value or nullopt.
     * 2. Array/Object: return pointer to heap object or nullptr.
     * 3. String: delegates to is_string() + as_string().
     */

    std::optional<bool> Json::try_as_boolean() const noexcept
    {
        if (m_type == Type::Boolean)
            return m_data.boolean;
        return std::nullopt;
    }

    std::optional<int64_t> Json::try_as_int() const noexcept
    {
        if (m_type == Type::Integer)
            return m_data.integer;
        return std::nullopt;
    }

    std::optional<double> Json::try_as_float() const noexcept
    {
        if (m_type == Type::Floating)
            return m_data.floating;
        return std::nullopt;
    }

    std::optional<std::string_view> Json::try_as_string() const noexcept
    {
        if (is_string())
            return as_string();
        return std::nullopt;
    }

    Array *Json::try_as_array() noexcept
    {
        if (m_type == Type::ArrayType)
            return static_cast<Array *>(m_data.heap);
        return nullptr;
    }

    const Array *Json::try_as_array() const noexcept
    {
        if (m_type == Type::ArrayType)
            return static_cast<const Array *>(m_data.heap);
        return nullptr;
    }

    Object *Json::try_as_object() noexcept
    {
        if (m_type == Type::ObjectType)
            return static_cast<Object *>(m_data.heap);
        return nullptr;
    }

    const Object *Json::try_as_object() const noexcept
    {
        if (m_type == Type::ObjectType)
            return static_cast<const Object *>(m_data.heap);
        return nullptr;
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
     * 1. Verify m_type is ArrayType or ObjectType (throw TypeError on mismatch).
     * 2. Delegate to the underlying container's accessor.
     *
     * operator[] skips bounds check (Array) or insert-if-missing (Object).
     * at() includes bounds check from the container.
     *
     * @throws TypeError if not the expected container type
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

    // --- begin/end/keys/values ---

    /*
     * Range-for dispatch: the container tag picks the iterator's underlying
     * storage. A scalar has no children — TypeError in both build modes
     * (the at()/operator[] family; deliberately NOT the as_* debug_check
     * track: a range-for on a scalar is a user error, not an unchecked
     * access). size() is a different contract (cardinality, scalar = 1).
     */
    JsonIterator Json::begin()
    {
        if (is_array())
            return JsonIterator(as_array().begin());
        if (is_object())
            return JsonIterator(as_object().begin());
        throw TypeError("expected array or object");
    }

    JsonIterator Json::end()
    {
        if (is_array())
            return JsonIterator(as_array().end());
        if (is_object())
            return JsonIterator(as_object().end());
        throw TypeError("expected array or object");
    }

    ConstJsonIterator Json::begin() const
    {
        if (is_array())
            return ConstJsonIterator(as_array().begin());
        if (is_object())
            return ConstJsonIterator(as_object().begin());
        throw TypeError("expected array or object");
    }

    ConstJsonIterator Json::end() const
    {
        if (is_array())
            return ConstJsonIterator(as_array().end());
        if (is_object())
            return ConstJsonIterator(as_object().end());
        throw TypeError("expected array or object");
    }

    KeysView Json::keys() const
    {
        if (!is_object())
            throw TypeError("expected object");
        return as_object().keys();
    }

    ValuesView Json::values()
    {
        if (!is_object())
            throw TypeError("expected object");
        return as_object().values();
    }

    ConstValuesView Json::values() const
    {
        if (!is_object())
            throw TypeError("expected object");
        return as_object().values();
    }

    // --- operator== ---

    /*
     * Equality comparison
     *
     * 1. Json-vs-Json: compare m_type first for fast rejection, then
     *    compare values by type.
     * 2. Json-vs-scalar: type-check first, then value comparison.
     * 3. StringView and StringOwned are compared by content (not storage).
     */
    bool Json::operator==(const Json &other) const
    {
        if (is_string() && other.is_string())
            return as_string() == other.as_string();
        if (m_type != other.m_type)
            return false;
        switch (m_type)
        {
        case Type::Null:
            return true;
        case Type::Boolean:
            return m_data.boolean == other.m_data.boolean;
        case Type::Integer:
            return m_data.integer == other.m_data.integer;
        case Type::Floating:
            return m_data.floating == other.m_data.floating;
        case Type::ArrayType:
            return as_array() == other.as_array();
        case Type::ObjectType:
            return as_object() == other.as_object();
        }
        return false;
    }

    bool Json::operator==(std::nullptr_t) const noexcept { return is_null(); }
    bool Json::operator==(bool val) const noexcept { return is_boolean() && as_boolean() == val; }
    bool Json::operator==(int64_t val) const noexcept { return is_int() && as_int() == val; }
    bool Json::operator==(double val) const noexcept { return is_float() && as_float() == val; }
    bool Json::operator==(std::string_view val) const noexcept { return is_string() && as_string() == val; }
    bool Json::operator==(const char *val) const noexcept { return operator==(std::string_view(val)); }
    bool Json::operator==(const Array &val) const { return is_array() && as_array() == val; }
    bool Json::operator==(const Object &val) const { return is_object() && as_object() == val; }
}

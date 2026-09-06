#include "pjh_json/object.hpp"
#include "pjh_json/json.hpp"

#include <algorithm>
#include <memory>
#include <ranges>
#include <string_view>
#include <utility>

namespace pjh::json
{
    /*
     * Construct empty Object with pmr allocator
     *
     * 1. Resolve allocator (default to global config resource).
     * 2. Initialize internal vector with the resolved resource.
     */
    Object::Object(std::pmr::memory_resource *res)
        : m_data(res ? res : Config::instance().resource()),
          m_resource(res ? res : Config::instance().resource())
    {
    }

    /*
     * Adopt existing vector
     *
     * Infer resource from vector's allocator, then move data in.
     */
    Object::Object(Vec val)
        : m_data(std::move(val)),
          m_resource(m_data.get_allocator().resource())
    {
    }

    /*
     * Deep copy each entry into a new Object with the target resource
     *
     * 1. Materialise key string into target resource.
     * 2. Recursively clone value.
     */
    Object Object::clone(std::pmr::memory_resource *into) const
    {
        Object out(into);
        out.m_data.reserve(m_data.size());
        for (const auto &[key, val] : m_data)
        {
            String k{static_cast<std::string_view>(key)};
            k.own(into);
            out.m_data.emplace_back(std::move(k), val.clone(into));
        }
        return out;
    }

    /*
     * Move construct — steal vector from source
     *
     * The source keeps its resource: its moved-from state (allocator
     * member still bound) stays consistent with m_resource, so it remains
     * adoptable (Json heap_alloc / destroy, json.hpp).
     */
    Object::Object(Object &&other) noexcept
        : m_data(std::move(other.m_data)),
          m_resource(other.m_resource)
    {
    }

    /*
     * Move assign
     *
     * pmr allocators never propagate on move-assign (all
     * propagate_on_container_* traits are false), so libstdc++ steals the
     * source's storage only when the allocators compare equal; otherwise it
     * element-wise moves into a fresh buffer allocated by this container's
     * own allocator, leaving the source with its own (now empty) buffer.
     * m_data's allocator member is self-consistent in both cases, so only
     * the wrapper's m_resource bookkeeping must be guarded:
     *
     * 1. Guard against self-assignment.
     * 2. Compare allocators before the move (mirrors libstdc++'s own
     *    steal decision in vector::_M_move_assign).
     * 3. Move the vector.
     * 4. Same resource: storage was stolen and both sides stay bound to
     *    the shared resource, so m_resource is copied (value-identical)
     *    instead of transferred: the source keeps it too, so a moved-from
     *    container remains adoptable (Json heap_alloc / destroy, json.hpp)
     *    into a node allocated in that shared resource.
     *    Different resources: keep this->m_resource (== m_data's allocator
     *    resource) so the node this container later heap-allocates into
     *    (Json::heap_alloc / Json::destroy, json.hpp) frees through the
     *    resource that owns this container's storage. The source keeps its
     *    m_resource because it keeps an empty buffer in that resource.
     */
    Object &Object::operator=(Object &&other) noexcept
    {
        if (this == &other)
            return *this;
        const bool same_resource =
            m_data.get_allocator() == other.m_data.get_allocator();
        m_data = std::move(other.m_data);
        if (same_resource)
            m_resource = other.m_resource;
        return *this;
    }

    /*
     * Linear search by key (insertion-order vector)
     */
    bool Object::contains(std::string_view key) const noexcept
    {
        return std::ranges::find_if(
                   m_data,
                   [&](const auto &kv)
                   { return kv.first == key; }) !=
               m_data.end();
    }

    /*
     * Mutable key access: find-or-insert
     *
     * 1. Search for existing key via linear scan.
     * 2. If found, return reference to value.
     * 3. If not found, append default-constructed Json entry and return it.
     */
    Json &Object::operator[](std::string_view key)
    {
        auto it = std::find_if(
            m_data.begin(), m_data.end(),
            [&](auto &kv)
            { return kv.first == key; });

        if (it == m_data.end())
        {
            m_data.emplace_back(key, Json());
            return m_data.back().second;
        }

        return it->second;
    }

    /*
     * Const key access: find-or-throw
     *
     * 1. Search for key via linear scan.
     * 2. If found, return const reference.
     * 3. If missing, throw out_of_range.
     */
    const Json &Object::operator[](std::string_view key) const
    {
        auto it = std::ranges::find_if(
            m_data.begin(), m_data.end(),
            [&](auto &kv)
            { return kv.first == key; });

        if (it == m_data.end())
            throw std::out_of_range("json key not found");

        return it->second;
    }

    /*
     * Mutable key access with bounds check: find-or-throw
     *
     * 1. Search for key via linear scan.
     * 2. If found, return mutable reference.
     * 3. If missing, throw out_of_range.
     */
    Json &Object::at(std::string_view key)
    {
        auto it = std::ranges::find_if(
            m_data.begin(), m_data.end(),
            [&](auto &kv)
            { return kv.first == key; });

        if (it == m_data.end())
            throw std::out_of_range("json key not found");

        return it->second;
    }

    /*
     * Const key access with bounds check: find-or-throw
     *
     * 1. Search for key via linear scan.
     * 2. If found, return const reference.
     * 3. If missing, throw out_of_range.
     */
    const Json &Object::at(std::string_view key) const
    {
        auto it = std::ranges::find_if(
            m_data.begin(), m_data.end(),
            [&](auto &kv)
            { return kv.first == key; });

        if (it == m_data.end())
            throw std::out_of_range("json key not found");

        return it->second;
    }

    /*
     * Insert or overwrite by key
     *
     * 1. Search for existing key.
     * 2. If found, overwrite its value.
     * 3. If not found, append new entry.
     */
    void Object::insert(std::string_view key, Json val)
    {
        auto it = std::ranges::find_if(
            m_data,
            [&](auto &kv) { return kv.first == key; });

        if (it != m_data.end())
        {
            it->second = std::move(val);
            return;
        }
        m_data.emplace_back(key, std::move(val));
    }

    /*
     * Insert or overwrite by key, owning the key
     *
     * 1. Resolve resource (null falls back to global config resource).
     * 2. Search for existing key (content compare, mode-agnostic).
     * 3. If found, overwrite its value; keep the old key.
     * 4. If not found, copy the key into an owned String via own(res) and
     *    append. The key buffer is freed back into res at destruction, so
     *    res must outlive this Object.
     */
    void Object::insert(std::string_view key, Json val,
                        std::pmr::memory_resource *res)
    {
        if (!res)
            res = Config::instance().resource();

        auto it = std::ranges::find_if(
            m_data,
            [&](auto &kv) { return kv.first == key; });

        if (it != m_data.end())
        {
            it->second = std::move(val);
            return;
        }

        String owned{key};
        owned.own(res);
        m_data.emplace_back(std::move(owned), std::move(val));
    }

    void Object::insert(Entry entry)
    {
        auto it = std::ranges::find_if(
            m_data,
            [&](auto &kv) { return kv.first == entry.first; });

        if (it != m_data.end())
        {
            it->second = std::move(entry.second);
            return;
        }
        m_data.push_back(std::move(entry));
    }

    /*
     * Remove key
     *
     * 1. Search for key.
     * 2. If found, erase entry and return true.
     * 3. If missing, return false.
     */
    bool Object::remove(std::string_view key)
    {
        auto it = std::ranges::find_if(
            m_data,
            [&](auto &kv) { return kv.first == key; });

        if (it == m_data.end())
            return false;

        m_data.erase(it);
        return true;
    }

    /*
     * Content equality (order-insensitive).
     *
     * 1. Fast reject on size mismatch.
     * 2. Sort pointers to entries by key, then compare element-wise.
     *    O(n log n) instead of O(n^2).
     */
    bool Object::operator==(const Object &other) const
    {
        if (size() != other.size())
            return false;
        std::vector<const Entry *> a, b;
        a.reserve(size());
        b.reserve(size());
        for (const auto &e : m_data) a.push_back(&e);
        for (const auto &e : other.m_data) b.push_back(&e);
        auto by_key = [](const Entry *x, const Entry *y) {
            return static_cast<std::string_view>(x->first) <
                   static_cast<std::string_view>(y->first);
        };
        std::sort(a.begin(), a.end(), by_key);
        std::sort(b.begin(), b.end(), by_key);
        for (size_t i = 0; i < a.size(); ++i)
        {
            if (a[i]->first != b[i]->first || a[i]->second != b[i]->second)
                return false;
        }
        return true;
    }

    size_t Object::size() const noexcept { return m_data.size(); }
    bool Object::empty() const noexcept { return m_data.empty(); }
    void Object::clear() noexcept { m_data.clear(); }
    Object::Vec::iterator Object::begin() noexcept { return m_data.begin(); }
    Object::Vec::iterator Object::end() noexcept { return m_data.end(); }
    Object::Vec::const_iterator Object::begin() const noexcept { return m_data.begin(); }
    Object::Vec::const_iterator Object::end() const noexcept { return m_data.end(); }
}

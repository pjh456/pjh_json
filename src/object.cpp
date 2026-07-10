#include "pjh_json/object.hpp"
#include "pjh_json/json.hpp"

#include <memory>
#include <ranges>
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
     * Move construct — steal vector and resource from source
     */
    Object::Object(Object &&other) noexcept
        : m_data(std::move(other.m_data)),
          m_resource(std::exchange(other.m_resource, nullptr))
    {
    }

    /*
     * Move ownership, null out source resource
     *
     * 1. Guard against self-assignment.
     * 2. Move the internal vector.
     * 3. Transfer resource pointer, null out source.
     */
    Object &Object::operator=(Object &&other) noexcept
    {
        if (this == &other)
            return *this;
        m_data = std::move(other.m_data);
        m_resource = std::exchange(other.m_resource, nullptr);
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
     * Order-sensitive equality
     *
     * 1. Fast reject on size mismatch.
     * 2. For each local entry, look up key in other and compare value.
     *
     * Equal only if same insertion order, keys, and values.
     */
    bool Object::operator==(const Object &other) const noexcept
    {
        if (size() != other.size())
            return false;
        for (const auto &[key, val] : m_data)
        {
            auto it = std::ranges::find_if(
                other.m_data,
                [&](const auto &kv) { return kv.first == key; });
            if (it == other.m_data.end() || it->second != val)
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

    Object::Vec &Object::data() noexcept { return m_data; }
    const Object::Vec &Object::data() const noexcept { return m_data; }
}

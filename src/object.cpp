#include "pjh_json/object.hpp"
#include "pjh_json/json.hpp"

#include <memory>
#include <ranges>
#include <utility>

namespace pjh::json
{
    struct Object::Impl
    {
        explicit Impl(std::pmr::memory_resource *res)
            : data(res) {}
        Vec data;
    };

    void Object::ImplDeleter::operator()(Impl *ptr) const noexcept
    {
        if (!ptr)
            return;
        std::pmr::polymorphic_allocator<Impl> alloc(res ? res : std::pmr::get_default_resource());
        std::destroy_at(ptr);
        alloc.deallocate(ptr, 1);
    }

    Object::Object(std::pmr::memory_resource *res)
        : m_impl(nullptr, ImplDeleter{res ? res : std::pmr::get_default_resource()}),
          m_resource(res ? res : std::pmr::get_default_resource())
    {
        std::pmr::polymorphic_allocator<Impl> alloc(m_resource);
        Impl *ptr = alloc.allocate(1);
        try
        {
            std::construct_at(ptr, m_resource);
        }
        catch (...)
        {
            alloc.deallocate(ptr, 1);
            throw;
        }
        m_impl.reset(ptr);
    }

    Object::Object(Vec val)
        : Object(val.get_allocator().resource())
    {
        m_impl->data = std::move(val);
    }

    Object::Object(std::initializer_list<Entry> items)
        : Object(std::pmr::get_default_resource())
    {
        m_impl->data = Vec(items, m_resource);
    }

    Object::~Object() = default;

    Object::Object(const Object &other)
        : Object(other.m_resource)
    {
        m_impl->data = other.m_impl->data;
    }

    Object &Object::operator=(const Object &other)
    {
        if (this == &other)
            return *this;
        Object tmp(other);
        *this = std::move(tmp);
        return *this;
    }

    Object::Object(Object &&other) noexcept
        : m_impl(std::move(other.m_impl)),
          m_resource(other.m_resource)
    {
        other.m_resource = std::pmr::get_default_resource();
    }

    Object &Object::operator=(Object &&other) noexcept
    {
        if (this == &other)
            return *this;
        m_impl = std::move(other.m_impl);
        m_resource = other.m_resource;
        other.m_resource = std::pmr::get_default_resource();
        return *this;
    }

    bool Object::contains(const Json &val) const noexcept
    {
        return std::ranges::find_if(
                   m_impl->data,
                   [&](const auto &kv)
                   { return kv.second == val; }) !=
               m_impl->data.end();
    }

    Json &Object::operator[](std::string_view key)
    {
        auto it = std::find_if(
            m_impl->data.begin(), m_impl->data.end(),
            [&](auto &kv)
            { return kv.first == key; });

        if (it == m_impl->data.end())
        {
            m_impl->data.emplace_back(key, Json());
            return m_impl->data.back().second;
        }

        return it->second;
    }

    const Json &Object::operator[](std::string_view key) const
    {
        auto it = std::ranges::find_if(
            m_impl->data.begin(), m_impl->data.end(),
            [&](auto &kv)
            { return kv.first == key; });

        if (it == m_impl->data.end())
            throw std::out_of_range("json key not found");

        return it->second;
    }

    Json &Object::at(std::string_view key)
    {
        auto it = std::ranges::find_if(
            m_impl->data.begin(), m_impl->data.end(),
            [&](auto &kv)
            { return kv.first == key; });

        if (it == m_impl->data.end())
            throw std::out_of_range("json key not found");

        return it->second;
    }

    const Json &Object::at(std::string_view key) const
    {
        auto it = std::ranges::find_if(
            m_impl->data.begin(), m_impl->data.end(),
            [&](auto &kv)
            { return kv.first == key; });

        if (it == m_impl->data.end())
            throw std::out_of_range("json key not found");

        return it->second;
    }

    void Object::insert(std::string_view key, Json val)
    {
        insert(std::make_pair(key, val));
    }

    void Object::insert(Entry entry)
    {
        m_impl->data.push_back(std::move(entry));
    }

    void Object::remove(std::string_view key)
    {
        auto it = std::ranges::find_if(
            m_impl->data.begin(), m_impl->data.end(),
            [&](auto &kv)
            { return kv.first == key; });

        if (it == m_impl->data.end())
            return;

        m_impl->data.erase(it);
    }

    bool Object::operator==(const Object &other) const noexcept
    {
        return std::equal(
            m_impl->data.begin(),
            m_impl->data.end(),
            other.m_impl->data.begin(),
            [](const Entry &x, const Entry &y)
            { return x.first == y.first && x.second == y.second; });
    }

    size_t Object::size() const noexcept { return m_impl->data.size(); }
    bool Object::empty() const noexcept { return m_impl->data.empty(); }
    void Object::clear() noexcept { m_impl->data.clear(); }
    Object::Vec &Object::data() noexcept { return m_impl->data; }
    const Object::Vec &Object::data() const noexcept { return m_impl->data; }
}

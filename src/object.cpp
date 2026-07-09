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
        std::pmr::polymorphic_allocator<Impl> alloc(res ? res : Config::instance().resource());
        std::destroy_at(ptr);
        alloc.deallocate(ptr, 1);
    }

    Object::Object(std::pmr::memory_resource *res)
        : m_impl(nullptr, ImplDeleter{res ? res : Config::instance().resource()}),
          m_resource(res ? res : Config::instance().resource())
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

    Object::~Object() = default;

    Object Object::clone(std::pmr::memory_resource *into) const
    {
        Object out(into);
        out.m_impl->data.reserve(m_impl->data.size());
        for (const auto &[key, val] : m_impl->data)
        {
            String k{static_cast<std::string_view>(key)};
            k.own(into);
            out.m_impl->data.emplace_back(std::move(k), val.clone(into));
        }
        return out;
    }

    Object::Object(Object &&other) noexcept
        : m_impl(std::move(other.m_impl)),
          m_resource(std::exchange(other.m_resource, nullptr))
    {
    }

    Object &Object::operator=(Object &&other) noexcept
    {
        if (this == &other)
            return *this;
        m_impl = std::move(other.m_impl);
        m_resource = std::exchange(other.m_resource, nullptr);
        return *this;
    }

    bool Object::contains(std::string_view key) const noexcept
    {
        return std::ranges::find_if(
                   m_impl->data,
                   [&](const auto &kv)
                   { return kv.first == key; }) !=
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
        auto it = std::ranges::find_if(
            m_impl->data,
            [&](auto &kv) { return kv.first == key; });

        if (it != m_impl->data.end())
        {
            it->second = std::move(val);
            return;
        }
        m_impl->data.emplace_back(key, std::move(val));
    }

    void Object::insert(Entry entry)
    {
        auto it = std::ranges::find_if(
            m_impl->data,
            [&](auto &kv) { return kv.first == entry.first; });

        if (it != m_impl->data.end())
        {
            it->second = std::move(entry.second);
            return;
        }
        m_impl->data.push_back(std::move(entry));
    }

    bool Object::remove(std::string_view key)
    {
        auto it = std::ranges::find_if(
            m_impl->data,
            [&](auto &kv) { return kv.first == key; });

        if (it == m_impl->data.end())
            return false;

        m_impl->data.erase(it);
        return true;
    }

    bool Object::operator==(const Object &other) const noexcept
    {
        if (size() != other.size())
            return false;
        for (const auto &[key, val] : m_impl->data)
        {
            auto it = std::ranges::find_if(
                other.m_impl->data,
                [&](const auto &kv) { return kv.first == key; });
            if (it == other.m_impl->data.end() || it->second != val)
                return false;
        }
        return true;
    }

    size_t Object::size() const noexcept { return m_impl->data.size(); }
    bool Object::empty() const noexcept { return m_impl->data.empty(); }
    void Object::clear() noexcept { m_impl->data.clear(); }
    Object::Vec::iterator Object::begin() noexcept { return m_impl->data.begin(); }
    Object::Vec::iterator Object::end() noexcept { return m_impl->data.end(); }
    Object::Vec::const_iterator Object::begin() const noexcept { return m_impl->data.begin(); }
    Object::Vec::const_iterator Object::end() const noexcept { return m_impl->data.end(); }

    Object::Vec &Object::data() noexcept { return m_impl->data; }
    const Object::Vec &Object::data() const noexcept { return m_impl->data; }
}

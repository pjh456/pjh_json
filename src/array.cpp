#include "pjh_json/array.hpp"
#include "pjh_json/json.hpp"

#include <memory>
#include <utility>

namespace pjh::json
{
    struct Array::Impl
    {
        explicit Impl(std::pmr::memory_resource *res)
            : data(res) {}
        Vec data;
    };

    void Array::ImplDeleter::operator()(Impl *ptr) const noexcept
    {
        if (!ptr)
            return;
        std::pmr::polymorphic_allocator<Impl> alloc(res ? res : std::pmr::get_default_resource());
        std::destroy_at(ptr);
        alloc.deallocate(ptr, 1);
    }

    Array::Array(std::pmr::memory_resource *res)
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

    Array::Array(Vec vec)
        : Array(vec.get_allocator().resource())
    {
        m_impl->data = std::move(vec);
    }

    Array::Array(std::initializer_list<Json> vec)
        : Array(std::pmr::get_default_resource())
    {
        m_impl->data = Vec(vec, m_resource);
    }

    Array::~Array() = default;

    Array::Array(const Array &other)
        : Array(other.m_resource)
    {
        m_impl->data = other.m_impl->data;
    }

    Array &Array::operator=(const Array &other)
    {
        if (this == &other)
            return *this;
        Array tmp(other);
        *this = std::move(tmp);
        return *this;
    }

    Array::Array(Array &&other) noexcept
        : m_impl(std::move(other.m_impl)),
          m_resource(other.m_resource)
    {
        other.m_resource = std::pmr::get_default_resource();
    }

    Array &Array::operator=(Array &&other) noexcept
    {
        if (this == &other)
            return *this;
        m_impl = std::move(other.m_impl);
        m_resource = other.m_resource;
        other.m_resource = std::pmr::get_default_resource();
        return *this;
    }

    size_t Array::size() const noexcept { return m_impl->data.size(); }
    bool Array::empty() const noexcept { return m_impl->data.empty(); }
    void Array::clear() noexcept { return m_impl->data.clear(); }
    bool Array::contains(const Json &val) const noexcept
    {
        return std::find(
                   m_impl->data.begin(),
                   m_impl->data.end(),
                   val) !=
               m_impl->data.end();
    }

    void Array::resize(size_t val) { m_impl->data.resize(val); }
    void Array::reserve(size_t val) { m_impl->data.reserve(val); }

    Array::Vec &Array::data() noexcept { return m_impl->data; }
    const Array::Vec &Array::data() const noexcept { return m_impl->data; }

    Json &Array::operator[](size_t idx) noexcept { return m_impl->data[idx]; }
    const Json &Array::operator[](size_t idx) const noexcept { return m_impl->data[idx]; }

    Json &Array::at(size_t idx) { return m_impl->data.at(idx); }
    const Json &Array::at(size_t idx) const { return m_impl->data.at(idx); }

    void Array::push_back(Json v) { return m_impl->data.push_back(std::move(v)); }
    void Array::erase(size_t idx, size_t len)
    {
        m_impl->data.erase(m_impl->data.begin() + idx, m_impl->data.begin() + idx + len);
    }

    bool Array::operator==(const Array &other) const noexcept
    {
        return std::equal(
            m_impl->data.begin(),
            m_impl->data.end(),
            other.m_impl->data.begin(),
            [](const Json &x, const Json &y)
            { return x == y; });
    }
}

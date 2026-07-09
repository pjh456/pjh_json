#include "pjh_json/array.hpp"
#include "pjh_json/json.hpp"

#include <memory>
#include <stdexcept>
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
        std::pmr::polymorphic_allocator<Impl> alloc(res ? res : Config::instance().resource());
        std::destroy_at(ptr);
        alloc.deallocate(ptr, 1);
    }

    Array::Array(std::pmr::memory_resource *res)
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

    Array::Array(Vec vec)
        : Array(vec.get_allocator().resource())
    {
        m_impl->data = std::move(vec);
    }

    Array::~Array() = default;

    Array Array::clone(std::pmr::memory_resource *into) const
    {
        Array out(into);
        out.reserve(m_impl->data.size());
        for (const Json &e : m_impl->data)
            out.push_back(e.clone(into));
        return out;
    }

    Array &Array::operator=(Array &&other) noexcept
    {
        if (this == &other)
            return *this;
        m_impl = std::move(other.m_impl);
        m_resource = std::exchange(other.m_resource, nullptr);
        return *this;
    }

    Array::Array(Array &&other) noexcept
        : m_impl(std::move(other.m_impl)),
          m_resource(std::exchange(other.m_resource, nullptr))
    {
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

    Array::Vec::iterator Array::begin() noexcept { return m_impl->data.begin(); }
    Array::Vec::iterator Array::end() noexcept { return m_impl->data.end(); }
    Array::Vec::const_iterator Array::begin() const noexcept { return m_impl->data.begin(); }
    Array::Vec::const_iterator Array::end() const noexcept { return m_impl->data.end(); }

    Array::Vec &Array::data() noexcept { return m_impl->data; }
    const Array::Vec &Array::data() const noexcept { return m_impl->data; }

    Json &Array::operator[](size_t idx) noexcept { return m_impl->data[idx]; }
    const Json &Array::operator[](size_t idx) const noexcept { return m_impl->data[idx]; }

    Json &Array::at(size_t idx) { return m_impl->data.at(idx); }
    const Json &Array::at(size_t idx) const { return m_impl->data.at(idx); }

    void Array::push_back(Json v) { return m_impl->data.push_back(std::move(v)); }
    void Array::erase(size_t idx, size_t len)
    {
        if (idx > m_impl->data.size() || len > m_impl->data.size() - idx)
            throw std::out_of_range("array erase out of range");
        m_impl->data.erase(m_impl->data.begin() + idx, m_impl->data.begin() + idx + len);
    }

    bool Array::operator==(const Array &other) const noexcept
    {
        if (m_impl->data.size() != other.m_impl->data.size())
            return false;
        return std::equal(
            m_impl->data.begin(),
            m_impl->data.end(),
            other.m_impl->data.begin(),
            [](const Json &x, const Json &y)
            { return x == y; });
    }
}

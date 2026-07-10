#include "pjh_json/array.hpp"
#include "pjh_json/json.hpp"

#include <memory>
#include <stdexcept>
#include <utility>

namespace pjh::json
{
    Array::Array(std::pmr::memory_resource *res)
        : m_data(res ? res : Config::instance().resource()),
          m_resource(res ? res : Config::instance().resource())
    {
    }

    Array::Array(Vec vec)
        : m_data(std::move(vec)),
          m_resource(m_data.get_allocator().resource())
    {
    }

    /*
     * Deep copy each element into a new Array with the target resource
     */
    Array Array::clone(std::pmr::memory_resource *into) const
    {
        Array out(into);
        out.reserve(m_data.size());
        for (const Json &e : m_data)
            out.push_back(e.clone(into));
        return out;
    }

    /*
     * Move ownership, null out source resource
     */
    Array &Array::operator=(Array &&other) noexcept
    {
        if (this == &other)
            return *this;
        m_data = std::move(other.m_data);
        m_resource = std::exchange(other.m_resource, nullptr);
        return *this;
    }

    Array::Array(Array &&other) noexcept
        : m_data(std::move(other.m_data)),
          m_resource(std::exchange(other.m_resource, nullptr))
    {
    }

    size_t Array::size() const noexcept { return m_data.size(); }
    bool Array::empty() const noexcept { return m_data.empty(); }
    void Array::clear() noexcept { return m_data.clear(); }

    // Linear search via std::find using Json operator==
    bool Array::contains(const Json &val) const noexcept
    {
        return std::find(
                   m_data.begin(),
                   m_data.end(),
                   val) !=
               m_data.end();
    }

    void Array::resize(size_t val) { m_data.resize(val); }
    void Array::reserve(size_t val) { m_data.reserve(val); }

    Array::Vec::iterator Array::begin() noexcept { return m_data.begin(); }
    Array::Vec::iterator Array::end() noexcept { return m_data.end(); }
    Array::Vec::const_iterator Array::begin() const noexcept { return m_data.begin(); }
    Array::Vec::const_iterator Array::end() const noexcept { return m_data.end(); }

    Array::Vec &Array::data() noexcept { return m_data; }
    const Array::Vec &Array::data() const noexcept { return m_data; }

    Json &Array::operator[](size_t idx) noexcept { return m_data[idx]; }
    const Json &Array::operator[](size_t idx) const noexcept { return m_data[idx]; }

    Json &Array::at(size_t idx) { return m_data.at(idx); }
    const Json &Array::at(size_t idx) const { return m_data.at(idx); }

    void Array::push_back(Json v) { return m_data.push_back(std::move(v)); }

    /*
     * Validate range, then erase [idx, idx+len)
     */
    void Array::erase(size_t idx, size_t len)
    {
        if (idx > m_data.size() || len > m_data.size() - idx)
            throw std::out_of_range("array erase out of range");
        m_data.erase(m_data.begin() + idx, m_data.begin() + idx + len);
    }

    /*
     * Compare sizes first for fast rejection, then element-by-element
     */
    bool Array::operator==(const Array &other) const noexcept
    {
        if (m_data.size() != other.m_data.size())
            return false;
        return std::equal(
            m_data.begin(),
            m_data.end(),
            other.m_data.begin(),
            [](const Json &x, const Json &y)
            { return x == y; });
    }
}

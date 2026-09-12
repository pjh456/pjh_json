#include "pjh_json/array.hpp"
#include "pjh_json/json.hpp"

#include <memory>
#include <stdexcept>
#include <utility>

namespace pjh::json
{
    /*
     * Construct empty Array with pmr allocator
     *
     * 1. Resolve allocator (default to global config resource).
     * 2. Initialize internal vector with the resolved resource.
     */
    Array::Array(std::pmr::memory_resource *res)
        : m_data(res ? res : Config::instance().resource()),
          m_resource(res ? res : Config::instance().resource())
    {
    }

    /*
     * Adopt existing vector
     *
     * Infer resource from vector's allocator, then move data in.
     */
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
        if (!into)
            into = Config::instance().resource();
        Array out(into);
        out.reserve(m_data.size());
        for (const Json &e : m_data)
            out.push_back(e.clone(into));
        return out;
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
     * 3. Move the vector, then explicitly clear the source. On the
     *    unequal-allocator path the standard leaves the source "valid but
     *    unspecified": libstdc++ empties it, but libc++/MSVC keep the
     *    moved-from elements in place (non-empty). The wrapper's contract
     *    (doxygen above) requires the moved-from source to be empty, so
     *    clear() it portably; it is a no-op when already empty and never
     *    deallocates the buffer (the source stays adoptable). clear() is
     *    noexcept, preserving the enclosing noexcept.
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
    Array &Array::operator=(Array &&other) noexcept
    {
        if (this == &other)
            return *this;
        const bool same_resource =
            m_data.get_allocator() == other.m_data.get_allocator();
        m_data = std::move(other.m_data);
        other.m_data.clear();
        if (same_resource)
            m_resource = other.m_resource;
        return *this;
    }

    /*
     * Move construct — steal vector from source
     *
     * The source keeps its resource: its moved-from state (allocator
     * member still bound) stays consistent with m_resource, so it remains
     * adoptable (Json heap_alloc / destroy, json.hpp).
     */
    Array::Array(Array &&other) noexcept
        : m_data(std::move(other.m_data)),
          m_resource(other.m_resource)
    {
    }

    size_t Array::size() const noexcept { return m_data.size(); }
    bool Array::empty() const noexcept { return m_data.empty(); }
    void Array::clear() noexcept { return m_data.clear(); }

    // Linear search via std::find using Json operator==. Not noexcept:
    // the comparison recurses into nested containers and may allocate.
    bool Array::contains(const Json &val) const
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
    bool Array::operator==(const Array &other) const
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

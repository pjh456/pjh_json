#ifndef PJH_JSON_COUNTING_RESOURCE_HPP
#define PJH_JSON_COUNTING_RESOURCE_HPP

#include <atomic>
#include <cstddef>
#include <memory>
#include <memory_resource>

namespace pjh::json
{
    /**
     * @brief Debug-only memory resource wrapper tracking outstanding allocations
     *
     * Wraps an upstream memory_resource and atomically counts the number of
     * outstanding (allocated - deallocated) blocks. Used by Config::release()
     * to assert that no objects allocated from the global resource are still
     * alive before releasing it.
     *
     * @note Only active in debug builds (NDEBUG not defined). count parameter
     *       to Document::make_arena() controls whether this wrapper is used.
     */
    class CountingResource : public std::pmr::memory_resource
    {
        std::unique_ptr<std::pmr::memory_resource> m_up;
        std::atomic<long long> m_count{0};

    public:
        /**
         * @brief Construct wrapping an upstream resource
         * @param up Upstream resource to delegate to (ownership taken)
         */
        explicit CountingResource(std::unique_ptr<std::pmr::memory_resource> up)
            : m_up(std::move(up)) {}

        /**
         * @brief Current number of outstanding (unfreed) allocations
         * @return Live allocation count (relaxed atomic load)
         */
        [[nodiscard]] long long outstanding() const noexcept
        {
            return m_count.load(std::memory_order_relaxed);
        }

    protected:
        /**
         * @brief Allocate from upstream, increment count
         * @param bytes Requested size
         * @param align Alignment requirement
         * @return Pointer to allocated memory
         */
        void *do_allocate(std::size_t bytes, std::size_t align) override
        {
            m_count.fetch_add(1, std::memory_order_relaxed);
            return m_up->allocate(bytes, align);
        }

        /**
         * @brief Deallocate via upstream, decrement count
         * @param p Pointer to deallocate
         * @param bytes Size of allocation
         * @param align Alignment of allocation
         */
        void do_deallocate(void *p, std::size_t bytes, std::size_t align) override
        {
            m_count.fetch_sub(1, std::memory_order_relaxed);
            m_up->deallocate(p, bytes, align);
        }

        /**
         * @brief Identity comparison (only equal to self)
         * @param o Other resource to compare
         * @return true if this == &o
         */
        [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource &o) const noexcept override
        {
            return this == &o;
        }
    };
}

#endif // PJH_JSON_COUNTING_RESOURCE_HPP

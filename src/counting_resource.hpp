#ifndef PJH_JSON_COUNTING_RESOURCE_HPP
#define PJH_JSON_COUNTING_RESOURCE_HPP

#include <atomic>
#include <cstddef>
#include <memory>
#include <memory_resource>

namespace pjh::json
{
    // 仅 Debug：包裹并拥有上游资源，统计未释放的分配数，供 Config::release() 断言
    class CountingResource : public std::pmr::memory_resource
    {
        std::unique_ptr<std::pmr::memory_resource> m_up;
        std::atomic<long long> m_count{0};

    public:
        explicit CountingResource(std::unique_ptr<std::pmr::memory_resource> up)
            : m_up(std::move(up)) {}

        [[nodiscard]] long long outstanding() const noexcept
        {
            return m_count.load(std::memory_order_relaxed);
        }

    protected:
        void *do_allocate(std::size_t bytes, std::size_t align) override
        {
            m_count.fetch_add(1, std::memory_order_relaxed);
            return m_up->allocate(bytes, align);
        }

        void do_deallocate(void *p, std::size_t bytes, std::size_t align) override
        {
            m_count.fetch_sub(1, std::memory_order_relaxed);
            m_up->deallocate(p, bytes, align);
        }

        [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource &o) const noexcept override
        {
            return this == &o;
        }
    };
}

#endif // PJH_JSON_COUNTING_RESOURCE_HPP

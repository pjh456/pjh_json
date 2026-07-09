#include "pjh_json/json.hpp"
#include "counting_resource.hpp"

#include <new>
#include <utility>

namespace pjh::json
{
    std::unique_ptr<std::pmr::memory_resource>
    Document::make_arena(Storage storage, size_t block, bool thread_safe, bool count)
    {
        std::unique_ptr<std::pmr::memory_resource> base;
        switch (storage)
        {
        case Storage::Pooled:
            if (thread_safe)
                base = std::make_unique<std::pmr::synchronized_pool_resource>(
                    std::pmr::pool_options{0, block}, std::pmr::new_delete_resource());
            else
                base = std::make_unique<std::pmr::unsynchronized_pool_resource>(
                    std::pmr::pool_options{0, block}, std::pmr::new_delete_resource());
            break;
        case Storage::Arena:
            base = std::make_unique<std::pmr::monotonic_buffer_resource>(
                block, std::pmr::new_delete_resource());
            break;
        case Storage::SystemDefault:
        default:
            return nullptr;
        }

#ifndef NDEBUG
        if (count)
            return std::make_unique<CountingResource>(std::move(base));
#else
        (void)count;
#endif
        return base;
    }

    Document::Document(Storage storage, size_t block, bool thread_safe, bool count)
        : Document(make_arena(storage, block, thread_safe, count),
                   Json(), std::pmr::string{}, false, storage, block, thread_safe, count)
    {
    }

    Document::Document(std::unique_ptr<std::pmr::memory_resource> arena,
                       Json &&root, std::pmr::string &&buffer, bool is_view,
                       Storage storage, size_t block, bool thread_safe, bool count)
        : m_arena(std::move(arena)),
          m_root(std::move(root)),
          m_buffer(std::move(buffer)),
          m_is_view(is_view),
          m_storage(storage),
          m_block(block),
          m_thread_safe(thread_safe),
          m_count(count)
    {
    }

    std::pmr::memory_resource *Document::resource() noexcept
    {
        return m_arena ? m_arena.get() : std::pmr::new_delete_resource();
    }

    void Document::reset()
    {
        *this = Document(m_storage, m_block, m_thread_safe, m_count);
    }

    Document &Document::operator=(Document &&other) noexcept
    {
        if (this != &other)
        {
            // move 构造会窃取并接管 allocator；赋值/swap 在 allocator 不等时会复制或 UB
            this->~Document();
            new (this) Document(std::move(other));
        }
        return *this;
    }
}

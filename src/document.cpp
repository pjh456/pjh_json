#include "pjh_json/document.hpp"
#include "counting_resource.hpp"

#include <new>
#include <utility>

namespace pjh::json
{
    /*
     * Create memory resource for parsed document
     *
     * 1. Switch by storage policy to select resource type:
     *    - Pooled: thread-safe or unsynchronized pool_resource.
     *    - Arena: monotonic_buffer_resource (no per-block deallocation).
     *    - SystemDefault: return nullptr (caller falls back to new_delete).
     * 2. Debug mode: optionally wrap in CountingResource for leak detection.
     */
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

    /*
     * Delegated constructor: make_arena then forward to full constructor
     */
    Document::Document(Storage storage, size_t block, bool thread_safe, bool count)
        : Document(make_arena(storage, block, thread_safe, count),
                   Json(), std::pmr::string{}, false, storage, block, thread_safe, count)
    {
    }

    /*
     * Full constructor: take ownership of arena, root, buffer, and settings
     */
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

    /*
     * Return the arena resource, or fall back to new_delete_resource
     */
    std::pmr::memory_resource *Document::resource() noexcept
    {
        return m_arena ? m_arena.get() : std::pmr::new_delete_resource();
    }

    /*
     * Reconstruct in-place: assign a fresh empty Document with same settings
     */
    void Document::reset()
    {
        *this = Document(m_storage, m_block, m_thread_safe, m_count);
    }

    /*
     * Move assignment via destruct + placement-new
     *
     * Directly moving unique_ptr (arena) and Json is safe across unequal
     * allocators. The old Document is destroyed first to prevent double-free
     * of arena resources when the source is destroyed.
     */
    Document &Document::operator=(Document &&other) noexcept
    {
        if (this != &other)
        {
            this->~Document();
            new (this) Document(std::move(other));
        }
        return *this;
    }
}

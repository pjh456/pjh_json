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
     * Move construct
     *
     * Memberwise in declaration order (identical to the defaulted form):
     * unique_ptr move nulls other.m_arena without destroying the arena
     * object; Json's move ctor (json.hpp:257-261) nulls other.m_root's tag;
     * the pmr string move steals storage and COPIES the allocator member,
     * leaving other.m_buffer's allocator pointing at the arena object that
     * moved into this document. That arena dies with its new owner — any
     * later string operation on the moved-from document would then
     * virtual-dispatch do_is_equal/do_allocate onto freed memory.
     *
     * Rebind the moved-from buffer to the immortal new_delete_resource,
     * verbatim the operator= source rebind, so the moved-from document is
     * self-contained no matter which of the two dies first. The explicit
     * destructor is trivially safe: the moved-from string is empty (local),
     * so its dispose skips deallocation and never touches the old member.
     */
    Document::Document(Document &&other) noexcept
        : m_arena(std::move(other.m_arena)),
          m_root(std::move(other.m_root)),
          m_buffer(std::move(other.m_buffer)),
          m_is_view(other.m_is_view),
          m_storage(other.m_storage),
          m_block(other.m_block),
          m_thread_safe(other.m_thread_safe),
          m_count(other.m_count)
    {
        using PmrString = std::pmr::string;
        other.m_buffer.~PmrString();
        ::new (static_cast<void *>(std::addressof(other.m_buffer)))
            PmrString(std::pmr::new_delete_resource());
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
     * Move assignment
     *
     * Invariants:
     * 1. The old root/buffer are destroyed through the old arena while it
     *    is still alive; m_arena moves last, so the old resource is
     *    destroyed only after nothing references it anymore.
     * 2. m_buffer's allocator member is rebound before the arena moves.
     *    String assignment never updates the allocator member (both
     *    propagate_on_container_copy_assignment and
     *    propagate_on_container_move_assignment are false for
     *    polymorphic_allocator), while every string operation compares
     *    allocators (virtual calls on the resource). A bare move-assign
     *    would therefore leave m_buffer pointing at the old arena after
     *    the m_arena move — a heap-use-after-free on the next comparison
     *    or destruction. Rebinding is done by destroying the member in
     *    place and move-constructing it with the source's resource, the
     *    only form that sets the allocator member.
     * 3. The moved-from source is left self-contained: its buffer is
     *    rebuilt bound to the immortal new_delete_resource, so it never
     *    keeps a pointer to the arena that moved into this document.
     */
    Document &Document::operator=(Document &&other) noexcept
    {
        if (this != &other)
        {
            using PmrString = std::pmr::string;
            std::pmr::memory_resource *res = other.resource();

            m_root = std::move(other.m_root);

            m_buffer.~PmrString();
            ::new (static_cast<void *>(std::addressof(m_buffer)))
                PmrString(std::move(other.m_buffer), res);

            m_arena = std::move(other.m_arena);

            other.m_buffer.~PmrString();
            ::new (static_cast<void *>(std::addressof(other.m_buffer)))
                PmrString(std::pmr::new_delete_resource());

            m_is_view = other.m_is_view;
            m_storage = other.m_storage;
            m_block = other.m_block;
            m_thread_safe = other.m_thread_safe;
            m_count = other.m_count;
        }
        return *this;
    }
}

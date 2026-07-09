#include "pjh_json/json.hpp"

#include <new>
#include <utility>

namespace pjh::json
{
    std::unique_ptr<std::pmr::memory_resource>
    Document::make_arena(Storage storage, size_t block, bool thread_safe)
    {
        switch (storage)
        {
        case Storage::Pooled:
            if (thread_safe)
                return std::make_unique<std::pmr::synchronized_pool_resource>(
                    std::pmr::pool_options{0, block}, std::pmr::new_delete_resource());
            return std::make_unique<std::pmr::unsynchronized_pool_resource>(
                std::pmr::pool_options{0, block}, std::pmr::new_delete_resource());
        case Storage::Arena:
            return std::make_unique<std::pmr::monotonic_buffer_resource>(
                block, std::pmr::new_delete_resource());
        case Storage::SystemDefault:
        default:
            return nullptr; // resource() 回退到 new_delete_resource
        }
    }

    Document::Document(Storage storage, size_t block)
        : Document(make_arena(storage, block, false),
                   Json(), std::pmr::string{}, false, storage, block)
    {
    }

    Document::Document(std::unique_ptr<std::pmr::memory_resource> arena,
                       Json &&root, std::pmr::string &&buffer, bool is_view,
                       Storage storage, size_t block)
        : m_arena(std::move(arena)),
          m_root(std::move(root)),
          m_buffer(std::move(buffer)), // move 构造：窃取，接管源 allocator，O(1)
          m_is_view(is_view),
          m_storage(storage),
          m_block(block)
    {
    }

    std::pmr::memory_resource *Document::resource() noexcept
    {
        return m_arena ? m_arena.get() : std::pmr::new_delete_resource();
    }

    void Document::reset()
    {
        // 整体重建：新对象持全新 arena，swap 后旧资源随临时对象按逆序安全析构
        *this = Document(m_storage, m_block);
    }

    Document &Document::operator=(Document &&other) noexcept
    {
        if (this != &other)
        {
            // move 构造是安全的（pmr 成员 move 构造会窃取并接管 allocator）；
            // move 赋值/ swap 在 allocator 不等时是复制或 UB，故显式析构 + 重建。
            this->~Document();
            new (this) Document(std::move(other));
        }
        return *this;
    }
}

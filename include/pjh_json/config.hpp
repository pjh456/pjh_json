#ifndef INCLUDE_PJH_JSON_CONFIG_HPP
#define INCLUDE_PJH_JSON_CONFIG_HPP

#include <atomic>
#include <cstddef>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <shared_mutex>

namespace pjh::json
{
    /**
     * @brief Memory allocation policy for JSON values
     */
    enum class Storage
    {
        Pooled,       // std::pmr::unsynchronized_pool_resource (default)
        Arena,        // std::pmr::monotonic_buffer_resource
        SystemDefault // std::pmr::new_delete_resource
    };

    class Document;

    /**
     * @brief Global configuration singleton (storage policy, global resource)
     *
     * The knobs — strict_duplicate_keys, arena_block_size, max_depth and
     * the storage policy — are lock-free atomics: their setters and getters
     * may be called concurrently from any thread. The mutex guards only the
     * global Document (allocator state and the resource-cache invalidation
     * window). Knob semantics are capture-at-start: a value set mid-operation
     * takes effect at the next operation start (strict_duplicate_keys is
     * read at each object entry, i.e. from the next object onward).
     * The global Document provides a shared memory resource used as the
     * default allocator throughout the library.
     */
    class Config
    {
    public:
        /**
         * @brief Access the global singleton
         * @return Reference to the global Config instance
         */
        static Config &instance();

        /**
         * @brief Set storage policy and block size
         * @param storage   Allocation policy (default: Pooled)
         * @param block_size Block/chunk size for Pooled/Arena (default 4096)
         * @note Does NOT affect already-parsed Documents.
         */
        void configure(
            Storage storage = Storage::Pooled,
            size_t block_size = 4096);

        /**
         * @brief Get current arena resource (never null)
         * @return Pointer to the global memory_resource
         * @note Lock-free in the common case: the pointer is cached atomically
         *       and only invalidated by release()/reset(); the shared lock is
         *       taken solely to re-cache after such an invalidation.
         */
        [[nodiscard]] std::pmr::memory_resource *resource() noexcept;
        /**
         * @brief Get current storage policy
         * @return Active Storage value
         */
        [[nodiscard]] Storage storage() const noexcept;

        /**
         * @brief Enable/disable duplicate key detection during parse
         * @note Lock-free: relaxed atomic store/load, safe to call
         *       concurrently with parse. The value is read at each object
         *       entry, so a change takes effect from the next object
         *       (capture-at-start granularity).
         * @note Default: false. When off, duplicate keys are allowed and
         *       resolve last-wins: the first occurrence keeps its key and
         *       position, its value is overwritten in place — the same
         *       semantics as Object::insert. When on, a duplicate key
         *       throws ParseError. JSON spec does not mandate rejection
         *       of duplicate keys.
         */
        void set_strict_duplicate_keys(bool enable) noexcept { m_strict_duplicate_keys.store(enable, std::memory_order_relaxed); }
        [[nodiscard]] bool strict_duplicate_keys() const noexcept { return m_strict_duplicate_keys.load(std::memory_order_relaxed); }

        /**
         * @brief Set arena initial block size for per-parse allocation (0 = auto)
         * @param bytes Arena buffer size in bytes. 0 (default) means
         *        auto-scale based on input size (input_size * 3, capped at 16 GB).
         *        Non-zero uses the given fixed size for every parse.
         * @note Only meaningful with Storage::Arena.
         * @note Lock-free: relaxed atomic store/load, safe to call
         *       concurrently with parse. The value is captured at parse
         *       start; a change takes effect from the next parse
         *       (capture-at-start granularity).
         */
        void set_arena_block_size(size_t bytes) noexcept { m_arena_block_size.store(bytes, std::memory_order_relaxed); }
        [[nodiscard]] size_t arena_block_size() const noexcept { return m_arena_block_size.load(std::memory_order_relaxed); }

        /**
         * @brief Set maximum nesting depth for parse and dump
         * @param depth Maximum number of nested containers (objects/arrays);
         *        0 (default) means unlimited.
         * @note Captured at parse start / dump start; a later call does not
         *       affect in-flight operations.
         * @note Lock-free: relaxed atomic store/load, safe to call
         *       concurrently with parse/dump.
         */
        void set_max_depth(size_t depth) noexcept { m_max_depth.store(depth, std::memory_order_relaxed); }
        [[nodiscard]] size_t max_depth() const noexcept { return m_max_depth.load(std::memory_order_relaxed); }

        /**
         * @brief Release global document
         * @note In debug builds, asserts no outstanding allocations from the
         *       global resource.
         */
        void release();
        /**
         * @brief Reset config to defaults and release global document
         * @note Equivalent to configure(Pooled, 4096),
         *       strict_duplicate_keys = false, arena_block_size = 0 (auto),
         *       max depth = 0 (unlimited), then release().
         * @note Knob restoration uses atomic stores, still performed under
         *       the lock, in service of release_locked().
         */
        void reset();

    private:
        /**
         * @brief Private ctor -- singleton only
         */
        Config();
        /**
         * @brief Private dtor
         */
        ~Config();
        Config(const Config &) = delete;
        Config &operator=(const Config &) = delete;

        /**
         * @brief Release global document (must hold m_mutex)
         */
        void release_locked();

        std::atomic<bool> m_strict_duplicate_keys{false};
        std::atomic<size_t> m_arena_block_size{0};
        std::atomic<size_t> m_max_depth{0};
        std::atomic<Storage> m_storage{Storage::Pooled};
        size_t m_block = 4096;
        std::unique_ptr<Document> m_global;
        // Fast-path cache of m_global->resource(): read lock-free in resource(),
        // invalidated by release()/reset() while holding m_mutex (reset()
        // rebuilds the arena, so the pointer changes).
        std::atomic<std::pmr::memory_resource *> m_cached_resource{nullptr};
        std::shared_mutex m_mutex;
    };
}

#endif

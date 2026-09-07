#include "pjh_json/config.hpp"
#include "pjh_json/document.hpp"
#include "counting_resource.hpp"

#include <cassert>

namespace pjh::json
{
    namespace
    {
        constexpr size_t kBlock = 4096;
    }

    /*
     * Singleton: function-local static for thread-safe initialisation (C++11+).
     */
    Config &Config::instance()
    {
        static Config cfg;
        return cfg;
    }

    /*
     * Constructor: create a global Document with pooled storage, thread-safe,
     * and allocation counting enabled (debug). This Document's resource is
     * the default allocator for all library operations.
     */
    Config::Config()
        : m_global(std::make_unique<Document>(Storage::Pooled, kBlock, true, true))
    {
        /*
         * Seed the fast-path cache. The singleton ctor completes before
         * instance() publishes the object, so no synchronization is needed.
         */
        m_cached_resource.store(m_global->resource(), std::memory_order_relaxed);
    }

    Config::~Config() = default;

    /*
     * Thread-safe update of storage settings via mutex.
     * Does NOT affect already-parsed Documents.
     */
    void Config::configure(Storage storage, size_t block_size)
    {
        std::lock_guard lock(m_mutex);
        m_storage = storage;
        m_block = block_size;
    }

    /*
     * Fast path: lock-free atomic load of the cached resource pointer, the
     * common case for default-argument call sites (Array/Object/clone/dump/
     * Parser). The cache is only cleared by release()/reset(), which hold the
     * exclusive lock and rebuild the arena, so the slow path — shared_lock,
     * re-read, re-cache — runs only right after such an invalidation.
     */
    std::pmr::memory_resource *Config::resource() noexcept
    {
        if (auto *r = m_cached_resource.load(std::memory_order_acquire))
            return r;
        std::shared_lock lock(m_mutex);
        auto *r = m_global->resource();
        m_cached_resource.store(r, std::memory_order_release);
        return r;
    }

    Storage Config::storage() const noexcept
    {
        return m_storage;
    }

    void Config::release()
    {
        std::lock_guard lock(m_mutex);
        release_locked();
    }

    /*
     * Reset to defaults: Pooled/4096, strict_duplicate_keys off,
     * arena_block_size 0 (auto), max depth 0 (unlimited), strip_bom
     * off, strict_utf8 off, then release.
     */
    void Config::reset()
    {
        std::lock_guard lock(m_mutex);
        m_storage = Storage::Pooled;
        m_block = 4096;
        m_strict_duplicate_keys.store(false, std::memory_order_relaxed);
        m_arena_block_size.store(0, std::memory_order_relaxed);
        m_max_depth.store(0, std::memory_order_relaxed);
        m_strip_bom.store(false, std::memory_order_relaxed);
        m_strict_utf8.store(false, std::memory_order_relaxed);
        release_locked();
    }

    /*
     * Release global document (must hold m_mutex).
     * Invalidate the fast-path cache before tearing the arena down: reset()
     * rebuilds the resource, so a stale cache would outlive the old object.
     * Readers seeing null fall back to the locked path and block until the
     * fresh resource is in place.
     * Debug check: if CountingResource is active, assert zero outstanding
     * allocations — ensures no dangling references from parsed objects.
     */
    void Config::release_locked()
    {
        m_cached_resource.store(nullptr, std::memory_order_release);
        #ifndef NDEBUG
        if (auto *cr = dynamic_cast<CountingResource *>(m_global->resource()))
            assert(cr->outstanding() == 0 &&
                   "Config::release()/reset(): outstanding allocations from global resource");
#endif
        m_global->reset();
    }
}

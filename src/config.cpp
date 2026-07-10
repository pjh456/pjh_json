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

    std::pmr::memory_resource *Config::resource() noexcept
    {
        std::shared_lock lock(m_mutex);
        return m_global->resource();
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
     * Reset to defaults: Pooled/4096, then release.
     */
    void Config::reset()
    {
        std::lock_guard lock(m_mutex);
        m_storage = Storage::Pooled;
        m_block = 4096;
        release_locked();
    }

    /*
     * Release global document (must hold m_mutex).
     * Debug check: if CountingResource is active, assert zero outstanding
     * allocations — ensures no dangling references from parsed objects.
     */
    void Config::release_locked()
    {
#ifndef NDEBUG
        if (auto *cr = dynamic_cast<CountingResource *>(m_global->resource()))
            assert(cr->outstanding() == 0 &&
                   "Config::release()/reset(): outstanding allocations from global resource");
#endif
        m_global->reset();
    }
}

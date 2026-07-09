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

    Config &Config::instance()
    {
        static Config cfg;
        return cfg;
    }

    Config::Config()
        : m_global(std::make_unique<Document>(Storage::Pooled, kBlock, true, true))
    {
    }

    Config::~Config() = default;

    void Config::configure(Storage storage, size_t block_size)
    {
        std::lock_guard lock(m_mutex);
        m_storage = storage;
        m_block = block_size;
    }

    std::pmr::memory_resource *Config::resource() noexcept
    {
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

    void Config::reset()
    {
        std::lock_guard lock(m_mutex);
        m_storage = Storage::Pooled;
        m_block = 4096;
        release_locked();
    }

    void Config::release_locked()
    {
#ifndef NDEBUG
        if (auto *cr = dynamic_cast<CountingResource *>(m_global->resource()))
            assert(cr->outstanding() == 0 &&
                   "Config::release()/reset(): 仍有从全局资源分配的存活对象");
#endif
        m_global->reset();
    }
}

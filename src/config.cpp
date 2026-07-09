#include "pjh_json/config.hpp"

namespace pjh::json
{
    Config &Config::instance()
    {
        static Config cfg;
        return cfg;
    }

    Config::Config() noexcept
        : m_resource(&m_pool)
    {}

    Config::~Config() = default;

    void Config::configure(Storage storage, size_t block_size)
    {
        std::lock_guard lock(m_mutex);
        m_storage = storage;
        m_block_size = block_size;
        rebuild();
    }

    void Config::configure_strings(Strings s) noexcept
    {
        std::lock_guard lock(m_mutex);
        m_strings = s;
    }

    std::pmr::memory_resource *Config::resource() noexcept
    {
        return m_resource;
    }

    Config::Strings Config::strings_mode() const noexcept
    {
        return m_strings;
    }

    void Config::release()
    {
        std::lock_guard lock(m_mutex);
        m_pool.release();
        m_arena.release();
    }

    void Config::reset()
    {
        std::lock_guard lock(m_mutex);
        m_arena.release();
        m_pool.release();
        m_block_size = 4096;
        rebuild();
    }

    void Config::rebuild()
    {
        // Reconstruct arena with new block size
        m_arena.~monotonic_buffer_resource();
        new (&m_arena) std::pmr::monotonic_buffer_resource(m_block_size);

        m_pool.~synchronized_pool_resource();
        new (&m_pool) std::pmr::synchronized_pool_resource(
            std::pmr::pool_options{0, m_block_size},
            std::pmr::new_delete_resource());

        switch (m_storage)
        {
        case Storage::Arena:
            m_resource = &m_arena;
            break;
        case Storage::Pooled:
            m_resource = &m_pool;
            break;
        case Storage::SystemDefault:
            m_resource = std::pmr::new_delete_resource();
            break;
        }
    }
}

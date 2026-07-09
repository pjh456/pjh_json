#ifndef INCLUDE_PJH_JSON_CONFIG_HPP
#define INCLUDE_PJH_JSON_CONFIG_HPP

#include <cstddef>
#include <memory_resource>
#include <mutex>

namespace pjh::json
{
    enum class Storage { Pooled, Arena, SystemDefault };

    class Config
    {
    public:
        enum class Strings { Owning, BorrowWhenPossible };

        static Config &instance();

        void configure(
            Storage storage = Storage::Pooled,
            size_t block_size = 4096);

        void configure_strings(Strings s) noexcept;

        [[nodiscard]] std::pmr::memory_resource *resource() noexcept;
        [[nodiscard]] Storage storage() const noexcept;
        [[nodiscard]] Strings strings_mode() const noexcept;

        void release();
        void reset();

    private:
        Config() noexcept;
        ~Config();
        Config(const Config &) = delete;
        Config &operator=(const Config &) = delete;

        void rebuild();

        Storage m_storage = Storage::Pooled;
        Strings m_strings = Strings::Owning;
        size_t m_block_size = 4096;

        std::pmr::monotonic_buffer_resource m_arena;
        std::pmr::synchronized_pool_resource m_pool;
        std::pmr::memory_resource *m_resource;
        std::mutex m_mutex;
    };
}

#endif

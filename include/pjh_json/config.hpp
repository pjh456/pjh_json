#ifndef INCLUDE_PJH_JSON_CONFIG_HPP
#define INCLUDE_PJH_JSON_CONFIG_HPP

#include <cstddef>
#include <memory>
#include <memory_resource>
#include <mutex>

namespace pjh::json
{
    enum class Storage
    {
        Pooled,
        Arena,
        SystemDefault
    };

    class Document;

    class Config
    {
    public:
        static Config &instance();

        void configure(
            Storage storage = Storage::Pooled,
            size_t block_size = 4096);

        [[nodiscard]] std::pmr::memory_resource *resource() noexcept;
        [[nodiscard]] Storage storage() const noexcept;

        void release();
        void reset();

    private:
        Config();
        ~Config();
        Config(const Config &) = delete;
        Config &operator=(const Config &) = delete;

        void release_locked();

        Storage m_storage = Storage::Pooled;
        size_t m_block = 4096;
        std::unique_ptr<Document> m_global;
        std::mutex m_mutex;
    };
}

#endif

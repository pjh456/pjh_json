#ifndef INCLUDE_PJH_JSON_CONFIG_HPP
#define INCLUDE_PJH_JSON_CONFIG_HPP

#include <atomic>
#include <cstddef>
#include <memory>
#include <memory_resource>
#include <mutex>

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
     * Thread-safe via internal mutex. The global Document provides a shared
     * memory resource used as the default allocator throughout the library.
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
         */
        [[nodiscard]] std::pmr::memory_resource *resource() noexcept;
        /**
         * @brief Get current storage policy
         * @return Active Storage value
         */
        [[nodiscard]] Storage storage() const noexcept;

        /**
         * @brief Enable/disable duplicate key detection during parse
         * @note Default: false (skip check for performance). JSON spec
         *       does not mandate rejection of duplicate keys.
         */
        void set_strict_duplicate_keys(bool enable) noexcept { m_strict_duplicate_keys = enable; }
        [[nodiscard]] bool strict_duplicate_keys() const noexcept { return m_strict_duplicate_keys; }

        /**
         * @brief Set arena initial block size for per-parse allocation (0 = auto)
         * @param bytes Arena buffer size in bytes. 0 (default) means
         *        auto-scale based on input size (input_size * 3, capped at 16 GB).
         *        Non-zero uses the given fixed size for every parse.
         * @note Only meaningful with Storage::Arena.
         */
        void set_arena_block_size(size_t bytes) noexcept { m_arena_block_size = bytes; }
        [[nodiscard]] size_t arena_block_size() const noexcept { return m_arena_block_size; }

        /**
         * @brief Release global document
         * @note In debug builds, asserts no outstanding allocations from the
         *       global resource.
         */
        void release();
        /**
         * @brief Reset config to defaults and release global document
         * @note Equivalent to configure(Pooled, 4096) then release().
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

        bool m_strict_duplicate_keys = false;
        size_t m_arena_block_size = 0;
        std::atomic<Storage> m_storage{Storage::Pooled};
        size_t m_block = 4096;
        std::unique_ptr<Document> m_global;
        std::mutex m_mutex;
    };
}

#endif

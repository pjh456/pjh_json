#ifndef INCLUDE_PJH_JSON_CONFIG_HPP
#define INCLUDE_PJH_JSON_CONFIG_HPP

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
         * @brief Set two-stage parsing threshold in bytes (0 = disabled)
         * @param bytes Files larger than this use SIMD two-stage parsing.
         *        Default 0 (always use recursive-descent for all sizes).
         *        Recommended: 65536 (64 KB) for good speed/memory tradeoff.
         * @note Two-stage parsing builds a structural index (~1 entry per
         *       10-20 bytes of input). Below the threshold, recursive-descent
         *       is faster and uses no extra memory.
         */
        void set_two_stage_threshold(size_t bytes) noexcept { m_two_stage_threshold = bytes; }
        [[nodiscard]] size_t two_stage_threshold() const noexcept { return m_two_stage_threshold; }

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
        size_t m_two_stage_threshold = 0;
        Storage m_storage = Storage::Pooled;
        size_t m_block = 4096;
        std::unique_ptr<Document> m_global;
        std::mutex m_mutex;
    };
}

#endif

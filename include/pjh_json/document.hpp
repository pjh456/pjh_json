#ifndef INCLUDE_PJH_JSON_DOCUMENT_HPP
#define INCLUDE_PJH_JSON_DOCUMENT_HPP

#include <memory>
#include <memory_resource>
#include <string_view>

#include "json.hpp"

namespace pjh::json
{
    /**
     * @brief Owns parsed JSON tree + arena + buffer lifecycle
     *
     * Holds the memory arena, the root Json value, and the raw input buffer.
     * Parsed strings borrow from the internal buffer (except in parse_view mode
     * where they borrow from the caller's memory).
     */
    class Document
    {
        std::unique_ptr<std::pmr::memory_resource> m_arena;
        Json m_root;
        std::pmr::string m_buffer;
        bool m_is_view = false;
        Storage m_storage = Storage::Pooled;
        size_t m_block = 4096;
        bool m_thread_safe = false;
        bool m_count = false;

    public:
        /**
         * @brief Create arena resource for given storage policy
         * @param storage Allocation strategy (Pooled/Arena/SystemDefault)
         * @param block Block/chunk size for Pooled and Arena
         * @param thread_safe true = synchronized_pool_resource
         * @param count  true = wrap in CountingResource (debug, NDEBUG only)
         * @return nullptr for SystemDefault, otherwise an allocated resource
         */
        [[nodiscard]] static std::unique_ptr<std::pmr::memory_resource>
        make_arena(Storage storage, size_t block, bool thread_safe, bool count = false);

        /**
         * @brief Construct empty document
         * @param storage Allocation strategy (default: global config)
         * @param block   Block size (default 4096)
         * @param thread_safe Use synchronized pools
         * @param count   Enable allocation counting (debug)
         */
        explicit Document(Storage storage = Config::instance().storage(),
                          size_t block = 4096, bool thread_safe = false, bool count = false);

        /**
         * @brief Construct from existing parsed data
         * @param arena    Memory resource (may be null for SystemDefault)
         * @param root     Parsed Json tree (moved in)
         * @param buffer   Raw input buffer (moved in, may be empty for parse_view)
         * @param is_view  true if root borrows from external memory
         * @param storage  Allocation policy used
         * @param block    Block size
         * @param thread_safe Thread safety setting
         * @param count    Counting setting
         */
        Document(std::unique_ptr<std::pmr::memory_resource> arena,
                 Json &&root, std::pmr::string &&buffer, bool is_view,
                 Storage storage, size_t block, bool thread_safe = false, bool count = false);

        /**
         * @brief Copy not allowed
         */
        Document(const Document &) = delete;
        /**
         * @brief Copy not allowed
         */
        Document &operator=(const Document &) = delete;
        /**
         * @brief Move construct (default)
         */
        Document(Document &&) noexcept = default;
        /**
         * @brief Move assign
         * @param other Source document (left empty)
         * @return *this
         * @note Destructs this then placement-new from other to handle
         *       potentially unequal allocators.
         */
        Document &operator=(Document &&other) noexcept;

        /**
         * @brief Get arena resource (never null)
         * @return m_arena if set, else std::pmr::new_delete_resource()
         */
        [[nodiscard]] std::pmr::memory_resource *resource() noexcept;
        /**
         * @brief Reconstruct as empty document with same settings
         * @note Preserves storage, block, thread_safe, count flags.
         */
        void reset();

        /**
         * @brief true if root references external memory (no buffer copy)
         * @return m_is_view flag
         */
        [[nodiscard]] bool is_view() const noexcept { return m_is_view; }
        /**
         * @brief Raw buffer backing the parsed JSON
         * @return Const ref to internal buffer
         */
        [[nodiscard]] const std::pmr::string &buffer() const noexcept { return m_buffer; }
        /**
         * @brief Mutable access to root Json value
         * @return Mutable ref to root
         */
        [[nodiscard]] Json &root() noexcept { return m_root; }
        /**
         * @brief Const access to root Json value
         * @return Const ref to root
         */
        [[nodiscard]] const Json &root() const noexcept { return m_root; }
    };

    /**
     * @brief Parse buffer with trailing 64-byte padding (moves buffer ownership)
     * @param buffer Padded pmr::string (must have 64 extra NUL bytes)
     * @param storage Allocation strategy (default: global config)
     * @return Document owning the parsed tree and buffer
     * @throws ParseError on invalid JSON
     * @note buffer is consumed (moved into Document). Strings borrow from it.
     * @note buffer.size() must be >= 64; the final 64 bytes are NUL sentinels.
     */
    [[nodiscard]] Document parse_in_situ(
        std::pmr::string &&buffer,
        Storage storage = Config::instance().storage());

    /**
     * @brief Parse copy of json string (pads internally)
     * @param json UTF-8 JSON text (copied and padded internally)
     * @param storage Allocation strategy (default: global config)
     * @return Document owning the parsed tree and a copy of json
     * @throws ParseError on invalid JSON
     * @note The input is copied into a padded buffer owned by Document.
     */
    [[nodiscard]] Document parse_copy(
        std::string_view json,
        Storage storage = Config::instance().storage());

    /**
     * @brief Parse string view (no copy, caller retains data lifetime)
     * @param data Pointer to JSON text
     * @param content_len Length of JSON text (data must have 64 extra NUL bytes
     *                    past content_len)
     * @param storage Allocation strategy (default: global config)
     * @return Document with root pointing into caller's memory
     * @throws ParseError on invalid JSON
     * @note Strings in the parsed tree borrow from data. Caller must keep
     *       data alive for the lifetime of Document and all derived Json/Array
     *       /Object/String values.
     */
    [[nodiscard]] Document parse_view(
        const char *data, size_t content_len,
        Storage storage = Config::instance().storage());

    /**
     * @brief Parse newline-delimited JSON (one array element per line)
     * @param input Multi-line text, each non-blank line is one JSON value
     * @param storage Allocation strategy (default: global config)
     * @return Document whose root is an Array of per-line parsed values
     * @throws ParseError on invalid JSON in any line
     * @note Blank lines and lines with only whitespace are skipped.
     *       Lines use \\n as delimiter; \\r before \\n is stripped.
     */
    [[nodiscard]] Document parse_jsonl(
        std::string_view input,
        Storage storage = Config::instance().storage());

    /**
     * @brief Parse JSON from file
     * @param filepath Path to file
     * @param storage Allocation strategy (default: global config)
     * @return Document owning the parsed tree and file content buffer
     * @throws ParseError if file cannot be opened, read, or contains invalid JSON
     */
    [[nodiscard]] Document parse_file(
        std::string_view filepath,
        Storage storage = Config::instance().storage());

    /// @cond DOXYGEN_SKIP
    template <class... Ts>
        requires(std::constructible_from<Json, Ts> && ...)
    inline Array Array::of(Ts &&...vals)
    {
        Array a;
        a.reserve(sizeof...(vals));
        (a.push_back(Json(std::forward<Ts>(vals))), ...);
        return a;
    }

    template <class... Es>
        requires(std::convertible_to<Es, Object::Entry> && ...)
    inline Object Object::of(Es &&...entries)
    {
        Object o;
        (o.insert(Object::Entry(std::forward<Es>(entries))), ...);
        return o;
    }
    /// @endcond
}

#endif

#ifndef INCLUDE_PJH_JSON_DOCUMENT_HPP
#define INCLUDE_PJH_JSON_DOCUMENT_HPP

#include <memory>
#include <memory_resource>
#include <string_view>

#include "json.hpp"

namespace pjh::json
{
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
        [[nodiscard]] static std::unique_ptr<std::pmr::memory_resource>
        make_arena(Storage storage, size_t block, bool thread_safe, bool count = false);

        explicit Document(Storage storage = Config::instance().storage(),
                          size_t block = 4096, bool thread_safe = false, bool count = false);

        Document(std::unique_ptr<std::pmr::memory_resource> arena,
                 Json &&root, std::pmr::string &&buffer, bool is_view,
                 Storage storage, size_t block, bool thread_safe = false, bool count = false);

        Document(const Document &) = delete;
        Document &operator=(const Document &) = delete;
        Document(Document &&) noexcept = default;
        Document &operator=(Document &&other) noexcept;

        [[nodiscard]] std::pmr::memory_resource *resource() noexcept;
        void reset();

        [[nodiscard]] bool is_view() const noexcept { return m_is_view; }
        [[nodiscard]] const std::pmr::string &buffer() const noexcept { return m_buffer; }
        [[nodiscard]] Json &root() noexcept { return m_root; }
        [[nodiscard]] const Json &root() const noexcept { return m_root; }
    };

    [[nodiscard]] Document parse_in_situ(
        std::pmr::string &&buffer,
        Storage storage = Config::instance().storage());

    [[nodiscard]] Document parse_copy(
        std::string_view json,
        Storage storage = Config::instance().storage());

    [[nodiscard]] Document parse_view(
        const char *data, size_t content_len,
        Storage storage = Config::instance().storage());

    [[nodiscard]] Document parse_jsonl(
        std::string_view input,
        Storage storage = Config::instance().storage());

    [[nodiscard]] Document parse_file(
        std::string_view filepath,
        Storage storage = Config::instance().storage());

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
}

#endif

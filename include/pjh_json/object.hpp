#ifndef INCLUDE_PJH_JSON_OBJECT_HPP
#define INCLUDE_PJH_JSON_OBJECT_HPP

#include <stdexcept>
#include <string>
#include <string_view>
#include <memory>
#include <ranges>
#include <memory_resource>
#include <vector>

#include "json_fwd.hpp"

namespace pjh::json
{
    class Json;

    class Object
    {
    public:
        // `Entry.first` is a `string_view` borrowing the Document buffer.
        // Must keep the originating Document alive while using Entry.
        using Entry = std::pair<std::string_view, Json>;
        using Vec = std::pmr::vector<Entry>;

    private:
        struct Impl;
        struct ImplDeleter
        {
            std::pmr::memory_resource *res{nullptr};
            void operator()(Impl *ptr) const noexcept;
        };
        std::unique_ptr<Impl, ImplDeleter> m_impl;
        std::pmr::memory_resource *m_resource{nullptr};

    public:
        Object(
            std::pmr::memory_resource *res = std::pmr::get_default_resource());
        Object(Vec val);
        Object(std::initializer_list<Entry> items);

        ~Object();

        Object(const Object &);
        Object &operator=(const Object &);

        Object(Object &&) noexcept;
        Object &operator=(Object &&) noexcept;

    public:
        [[nodiscard]] size_t size() const noexcept;
        [[nodiscard]] bool empty() const noexcept;
        void clear() noexcept;
        [[nodiscard]] bool contains(std::string_view key) const noexcept;

        [[nodiscard]] Vec &data() noexcept;
        [[nodiscard]] const Vec &data() const noexcept;

    public:
        Json &operator[](std::string_view key);
        const Json &operator[](std::string_view key) const;

        Json &at(std::string_view key);
        const Json &at(std::string_view key) const;

    public:
        void insert(std::string_view key, Json val);
        void insert(Entry entry);

        bool remove(std::string_view key);

    public:
        [[nodiscard]] bool operator==(const Object &other) const noexcept;
    };

}

#endif // INCLUDE_PJH_JSON_OBJECT_HPP

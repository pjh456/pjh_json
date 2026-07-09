#ifndef INCLUDE_PJH_JSON_OBJECT_HPP
#define INCLUDE_PJH_JSON_OBJECT_HPP

#include <stdexcept>
#include <string>
#include <string_view>
#include <memory>
#include <ranges>
#include <memory_resource>
#include <vector>
#include <concepts>
#include <utility>

#include "config.hpp"

#include "json_fwd.hpp"

namespace pjh::json
{
    class Json;

    class Object
    {
    public:
        using Entry = std::pair<String, Json>;
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
            std::pmr::memory_resource *res = Config::instance().resource());
        Object(Vec val);

        ~Object();

        Object(const Object &) = delete;
        Object &operator=(const Object &) = delete;

        Object(Object &&) noexcept;
        Object &operator=(Object &&) noexcept;

        // 显式深拷贝
        [[nodiscard]] Object clone(
            std::pmr::memory_resource *into = Config::instance().resource()) const;

        // 可变参构建器；每个实参须可转为 Entry
        template <class... Es>
            requires(std::convertible_to<Es, Object::Entry> && ...)
        [[nodiscard]] static Object of(Es &&...entries);

    public:
        [[nodiscard]] size_t size() const noexcept;
        [[nodiscard]] bool empty() const noexcept;
        void clear() noexcept;
        [[nodiscard]] bool contains(std::string_view key) const noexcept;

        [[nodiscard]] Vec::iterator begin() noexcept;
        [[nodiscard]] Vec::iterator end() noexcept;
        [[nodiscard]] Vec::const_iterator begin() const noexcept;
        [[nodiscard]] Vec::const_iterator end() const noexcept;

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

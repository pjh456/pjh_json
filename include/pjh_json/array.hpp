#ifndef INCLUDE_PJH_JSON_ARRAY_HPP
#define INCLUDE_PJH_JSON_ARRAY_HPP

#include <algorithm>
#include <initializer_list>
#include <memory>
#include <memory_resource>
#include <vector>

#include "json_fwd.hpp"
#include "config.hpp"

namespace pjh::json
{
    class Json;

    class Array
    {
    public:
        using Vec = std::pmr::vector<Json>;

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
        Array(
            std::pmr::memory_resource *res = Config::instance().resource());
        Array(Vec vec);
        Array(std::initializer_list<Json> vec);

        ~Array();

        Array(const Array &);
        Array &operator=(const Array &);

        Array(Array &&) noexcept;
        Array &operator=(Array &&) noexcept;

    public:
        [[nodiscard]] size_t size() const noexcept;
        [[nodiscard]] bool empty() const noexcept;
        void clear() noexcept;
        [[nodiscard]] bool contains(const Json &val) const noexcept;

        void resize(size_t val);
        void reserve(size_t val);

        [[nodiscard]] Vec::iterator begin() noexcept;
        [[nodiscard]] Vec::iterator end() noexcept;
        [[nodiscard]] Vec::const_iterator begin() const noexcept;
        [[nodiscard]] Vec::const_iterator end() const noexcept;

        [[nodiscard]] Vec &data() noexcept;
        [[nodiscard]] const Vec &data() const noexcept;

    public:
        Json &operator[](size_t idx) noexcept;
        const Json &operator[](size_t idx) const noexcept;

        Json &at(size_t idx);
        const Json &at(size_t idx) const;

        void push_back(Json v);
        void erase(size_t idx, size_t len = 1);

    public:
        [[nodiscard]] bool operator==(const Array &other) const noexcept;
    };

}

#endif // INCLUDE_PJH_JSON_ARRAY_HPP

#ifndef INCLUDE_PJH_JSON_STRING_HPP
#define INCLUDE_PJH_JSON_STRING_HPP

#include <cstdint>
#include <string_view>
#include <memory_resource>
#include <string>

namespace pjh::json
{
    /**
     * @brief JSON string: either a view (borrowed) or owned (pmr::string*)
     *
     * Borrowed strings store a {ptr, len} pair inline (no allocation).
     * Owned strings store a pointer to a heap-allocated pmr::string.
     */
    class String
    {
        bool m_owned = false;

        struct ViewData
        {
            const char *data;
            uint32_t length;
        };

        union
        {
            ViewData m_view;           // 12 bytes, valid when !m_owned
            std::pmr::string *m_heap;  // 8 bytes,  valid when m_owned
        };

    public:
        String() noexcept : m_view{nullptr, 0} {}

        /**
         * @brief Borrowed view (no copy)
         * @param sv Source view -- caller must guarantee lifetime
         */
        String(std::string_view sv) noexcept : m_owned(false)
        {
            m_view.data = sv.data();
            m_view.length = static_cast<uint32_t>(sv.size());
        }

        /**
         * @brief Borrowed view from C string
         * @param s NUL-terminated source -- caller must guarantee lifetime
         */
        String(const char *s) noexcept : m_owned(false)
        {
            std::string_view sv(s);
            m_view.data = sv.data();
            m_view.length = static_cast<uint32_t>(sv.size());
        }

        /**
         * @brief Owned string (takes ownership of heap allocation)
         * @param s pmr::string to take ownership of (must be heap-allocated)
         */
        String(std::pmr::string *s) noexcept : m_owned(true), m_heap(s) {}

        /**
         * @brief Destructor -- frees owned string
         */
        ~String()
        {
            if (m_owned)
            {
                delete m_heap;
                m_owned = false;
            }
        }

        // Move-only semantics
        String(const String &) = delete;
        String &operator=(const String &) = delete;

        String(String &&other) noexcept : m_owned(other.m_owned)
        {
            if (other.m_owned)
                m_heap = other.m_heap;
            else
                m_view = other.m_view;
            other.m_owned = false;
        }

        String &operator=(String &&other) noexcept
        {
            if (this != &other)
            {
                this->~String();
                m_owned = other.m_owned;
                if (other.m_owned)
                    m_heap = other.m_heap;
                else
                    m_view = other.m_view;
                other.m_owned = false;
            }
            return *this;
        }

        /**
         * @brief Implicit conversion to string_view
         */
        [[nodiscard]] operator std::string_view() const noexcept
        {
            if (m_owned)
                return *m_heap;
            return std::string_view(m_view.data, m_view.length);
        }

        /**
         * @brief true if data is owned, not borrowed
         */
        [[nodiscard]] bool is_owned() const noexcept { return m_owned; }

        /**
         * @brief Convert to owned copy if not already owned
         * @param res Memory resource for the copy
         */
        void own(std::pmr::memory_resource *res = std::pmr::get_default_resource())
        {
            if (!m_owned)
            {
                auto sv = std::string_view(m_view.data, m_view.length);
                m_heap = new std::pmr::string(sv, res);
                m_owned = true;
            }
        }

        /**
         * @brief Release ownership of the internal pmr::string (if owned)
         * @return Pointer to owned pmr::string, or nullptr if borrowed.
         *         Caller takes ownership and must delete.
         */
        [[nodiscard]] std::pmr::string *release() noexcept
        {
            if (m_owned)
            {
                auto *p = m_heap;
                m_owned = false;
                return p;
            }
            return nullptr;
        }

        /**
         * @brief Compare with another String (content only)
         */
        [[nodiscard]] bool operator==(const String &other) const noexcept
        {
            return static_cast<std::string_view>(*this) == static_cast<std::string_view>(other);
        }

        /**
         * @brief Compare with string_view
         */
        [[nodiscard]] bool operator==(std::string_view other) const noexcept
        {
            return static_cast<std::string_view>(*this) == other;
        }

        /**
         * @brief Compare with C string
         */
        [[nodiscard]] bool operator==(const char *other) const noexcept
        {
            return static_cast<std::string_view>(*this) == other;
        }
    };

}

#endif

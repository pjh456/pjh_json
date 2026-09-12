#ifndef INCLUDE_PJH_JSON_STRING_HPP
#define INCLUDE_PJH_JSON_STRING_HPP

#include <cstdint>
#include <string_view>
#include <memory>
#include <memory_resource>
#include <new>
#include <string>
#include <type_traits>

#include "config.hpp"

namespace pjh::json
{
    class Json; // fwd: friend for the owned-header allocator pair

    /**
     * @brief JSON string — view (borrowed) or arena-allocated (owned).
     *
     * Replaces std::variant<string_view, pmr::string> with a manual tagged
     * union to reduce size from 40 to 16 bytes. Borrowed strings store a
     * {ptr, len} pair inline. Owned strings store a pointer to a
     * pmr::string whose object header *and* buffer both live in the
     * allocating memory resource — no global new/delete on either.
     *
     * Move-only — copy is deleted.
     */
    class String
    {
    private:
        /**
         * @brief Storage mode for the active union member.
         */
        enum class Storage : uint8_t
        {
            View = 0, // borrowed: m_data.view_data active
            Owned = 1 // owned:    m_data.heap_ptr active
        };

        Storage m_storage = Storage::View;

        struct ViewData
        {
            const char *data;
            uint32_t length;
        };

        /**
         * @brief Inline storage. Only one member is active per m_storage.
         *
         * | m_storage | active member | content                    |
         * |-----------|---------------|----------------------------|
         * | View      | view_data     | {ptr, len} inline          |
         * | Owned     | heap_ptr      | pmr::string* in resource   |
         */
        union
        {
            ViewData view_data;         // borrowed
            std::pmr::string *heap_ptr; // owned (object header in res)
        };

    public:
        /**
         * @brief Construct empty (null view)
         */
        constexpr String() noexcept : view_data{nullptr, 0} {}

        /**
         * @brief Borrowed view from string_view (no copy)
         * @param sv Source view — caller must guarantee lifetime
         * @note Stores {ptr, len} inline. Does NOT copy.
         */
        constexpr String(std::string_view sv) noexcept : m_storage(Storage::View)
        {
            view_data.data = sv.data();
            view_data.length = static_cast<uint32_t>(sv.size());
        }

        /**
         * @brief Borrowed view from C string (no copy)
         * @param s NUL-terminated source — caller must guarantee lifetime
         * @note Wraps s in string_view. Does NOT copy.
         */
        constexpr String(const char *s) noexcept : m_storage(Storage::View)
        {
            std::string_view sv(s);
            view_data.data = sv.data();
            view_data.length = static_cast<uint32_t>(sv.size());
        }

        /**
         * @brief Owned string (takes ownership of an allocator-produced header)
         * @param s Pointer to a pmr::string whose object header was allocated
         *          by String::make_owned()/own()/release() — i.e. through the
         *          same memory resource as the string's own allocator.
         * @note Caller transfers ownership; the destructor frees the header
         *       and its buffer through that resource (destroy_owned).
         * @warning Hand-built pointers from an external `new std::pmr::string`
         *          are NOT supported: destroy_owned deallocates the header
         *          through the string's own resource, which would mismatch a
         *          global operator new.
         */
        String(std::pmr::string *s) noexcept
            : m_storage(Storage::Owned), heap_ptr(s) {}

    private:
        /**
         * @brief Allocate a pmr::string object header through `res` and
         *        construct it in place.
         *
         * Both the object header and the string's content buffer are owned
         * by `res`. Strong guarantee: if construction throws (e.g. the
         * buffer allocation fails), the raw header block is deallocated
         * before the exception propagates.
         *
         * @param sv Source view — content is copied
         * @param res Memory resource for header and buffer (must be non-null)
         * @return Pointer to the constructed pmr::string
         */
        static std::pmr::string *make_owned(std::string_view sv,
                                            std::pmr::memory_resource *res)
        {
            std::pmr::polymorphic_allocator<std::pmr::string> alloc(res);
            std::pmr::string *p = alloc.allocate(1);
            try
            {
                std::construct_at(p, sv, res);
            }
            catch (...)
            {
                alloc.deallocate(p, 1);
                throw;
            }
            return p;
        }

        friend class Json;

    public:
        /**
         * @brief Destroy a pmr::string header produced by make_owned()/own()/
         *        release().
         *
         * Recovers the memory resource from the string's own allocator
         * (move preserves it, so it is exactly the resource the header came
         * from), runs the destructor (freeing the content buffer/proxy), then
         * deallocates the header block through that same resource. This is
         * the required release pair for release() — never `delete` the
         * pointer, which would free an allocator block through the global
         * operator delete.
         *
         * @param p Header to destroy (must be non-null)
         */
        static void destroy_owned(std::pmr::string *p) noexcept
        {
            std::pmr::memory_resource *res = p->get_allocator().resource();
            std::pmr::polymorphic_allocator<std::pmr::string> alloc(res);
            std::destroy_at(p);
            alloc.deallocate(p, 1);
        }

        /**
         * @brief Destructor — destroys the owned pmr::string header if present
         */
        constexpr ~String()
        {
            if (!std::is_constant_evaluated())
            {
                if (m_storage == Storage::Owned)
                {
                    destroy_owned(heap_ptr);
                    m_storage = Storage::View;
                }
            }
        }

        /**
         * @brief Copy not allowed — use own() to materialise
         */
        String(const String &) = delete;
        /**
         * @brief Copy not allowed — use own() to materialise
         */
        String &operator=(const String &) = delete;

        /**
         * @brief Move construct (steals active member, marks source empty)
         * @param other Source (left as an empty view: as_string_view().data()
         *        == nullptr, whether it was View or Owned)
         */
        constexpr String(String &&other) noexcept : m_storage(other.m_storage)
        {
            if (other.m_storage == Storage::Owned)
                heap_ptr = other.heap_ptr;
            else
                view_data = other.view_data;
            other.m_storage = Storage::View;
            other.view_data = ViewData{nullptr, 0};
        }

        /**
         * @brief Move assign (destroys old value, rebuilds from source)
         * @param other Source (left as an empty view: as_string_view().data()
         *        == nullptr, whether it was View or Owned)
         * @return *this
         */
        String &operator=(String &&other) noexcept
        {
            if (this != &other)
            {
                this->~String();
                ::new (static_cast<void *>(this)) String(std::move(other));
            }
            return *this;
        }

        /**
         * @brief Implicit conversion to string_view
         * @return View of content (works for both borrowed and owned)
         */
        [[nodiscard]] constexpr operator std::string_view() const noexcept
        {
            if (m_storage == Storage::Owned)
                return *heap_ptr;
            return std::string_view(view_data.data, view_data.length);
        }

        /**
         * @brief true if data is owned (pmr::string on heap), not borrowed
         */
        [[nodiscard]] constexpr bool is_owned() const noexcept
        {
            return m_storage == Storage::Owned;
        }

        /**
         * @brief Materialise borrowed view as owned copy if not already owned
         *
         * 1. If already owned: no-op.
         * 2. If borrowed: allocate a new pmr::string (object header + content
         *    buffer) from the given resource, copy the view content into it,
         *    store the pointer.
         *
         * @param res Memory resource for header and copy (default: global
         *        config resource; nullptr also falls back to it)
         * @note Safe to call multiple times; subsequent calls are no-ops.
         * @note `res` must outlive this String (or its ownership successor):
         *       both the pmr::string header and its buffer deallocate back
         *       into `res` on destruction.
         */
        void own(std::pmr::memory_resource *res = Config::instance().resource())
        {
            if (m_storage == Storage::View)
            {
                if (!res)
                    res = Config::instance().resource();
                auto sv = std::string_view(view_data.data, view_data.length);
                heap_ptr = make_owned(sv, res);
                m_storage = Storage::Owned;
            }
        }

        /**
         * @brief Release ownership of the internal pmr::string.
         *
         * After this call, the String becomes an empty view
         * (`as_string_view().data() == nullptr`). The caller takes ownership
         * of the returned pointer and must free it with
         * String::destroy_owned(p) — NOT `delete`: the header was allocated
         * through a memory resource, and destroy_owned recovers that resource
         * from the string's own allocator.
         *
         * @return Pointer to owned pmr::string, or nullptr if was borrowed
         *         (in which case the borrowed content is left untouched).
         */
        [[nodiscard]] constexpr std::pmr::string *release() noexcept
        {
            if (m_storage == Storage::Owned)
            {
                auto *p = heap_ptr;
                m_storage = Storage::View;
                view_data = ViewData{nullptr, 0};
                return p;
            }
            return nullptr;
        }

        /**
         * @brief Compare with another String (content only, not storage)
         * @param other String to compare
         * @return true if content is identical
         */
        [[nodiscard]] constexpr bool operator==(const String &other) const noexcept
        {
            return static_cast<std::string_view>(*this) == static_cast<std::string_view>(other);
        }

        /**
         * @brief Compare with string_view
         * @param other View to compare
         * @return true if content matches
         */
        [[nodiscard]] constexpr bool operator==(std::string_view other) const noexcept
        {
            return static_cast<std::string_view>(*this) == other;
        }

        /**
         * @brief Compare with C string
         * @param other NUL-terminated string to compare
         * @return true if content matches
         */
        [[nodiscard]] constexpr bool operator==(const char *other) const noexcept
        {
            return static_cast<std::string_view>(*this) == other;
        }
    };

}

#endif

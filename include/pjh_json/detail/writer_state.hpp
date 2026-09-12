#ifndef INCLUDE_PJH_JSON_DETAIL_WRITER_STATE_HPP
#define INCLUDE_PJH_JSON_DETAIL_WRITER_STATE_HPP

#include "pjh_json/error.hpp"
#include "pjh_json/writer.hpp"

#include <cstddef>
#include <memory_resource>
#include <string>
#include <string_view>

namespace pjh::json
{
    /*
     * Fixed-size, zero-allocation writer failure slot.
     *
     * The writer is a stateless free-function family, so the parser's member
     * slot becomes an explicit object threaded through the write kernel.
     * First failure wins (mirrors "the first throw unwinds the stack").
     * Every writer failure is context-free: Category::Json, positioned=false,
     * position=0 -- JsonError has no offset() channel.
     */
    struct DumpState
    {
        Error error{};

        [[nodiscard]] bool failed() const noexcept { return error.has_error(); }

        void fail(ErrorCode c, std::string_view detail = {}) noexcept
        {
            if (!error.has_error())
                error = Error{c, Category::Json, 0, false, detail};
        }
    };

    /*
     * Writer kernel driver: serialize @p value at depth 0 with
     * Config::instance().max_depth() into @p sink.
     * @return false on failure; the first failure is in @p st. On false the
     *         caller must not append further bytes and must materialise the
     *         error (throw JsonError(st.error) / Result Err) immediately.
     */
    [[nodiscard]] bool dump_value_to(std::pmr::string &sink, const Json &value,
                                     const DumpOptions &opts, DumpState &st);

    /*
     * Escape-and-quote kernel (write_escaped's body). false => st.error set;
     * @p detail is never used (all six UTF-8 codes are detail-less).
     */
    [[nodiscard]] bool write_escaped_impl(std::pmr::string &sink, std::string_view s,
                                          bool ascii, DumpState &st);
}

#endif // INCLUDE_PJH_JSON_DETAIL_WRITER_STATE_HPP

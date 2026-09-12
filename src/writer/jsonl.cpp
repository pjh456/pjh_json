#include "pjh_json/writer.hpp"
#include "pjh_json/detail/writer_state.hpp"

namespace pjh::json
{
    /*
     * JSONL dump kernel: serialise each array element compactly, one per
     * line. First failure wins and aborts the loop; the owned JsonError is
     * materialised in this frame (no borrowed detail here).
     */
    static pjh::result::Result<std::pmr::string, JsonError>
    dump_jsonl_impl(const Array &arr, std::pmr::memory_resource *res)
    {
        DumpOptions compact{};
        DumpState st;
        std::pmr::string sink(res);
        for (const auto &el : arr)
        {
            if (!dump_value_to(sink, el, compact, st))
                return pjh::result::Result<std::pmr::string, JsonError>::Err(
                    JsonError(st.error));
            sink.push_back('\n');
        }
        return pjh::result::Result<std::pmr::string, JsonError>::Ok(
            std::move(sink));
    }

    /*
     * JSONL dump: write each array element as a compact JSON line
     *
     * 1. Serialise each element via dump_value_to with compact options.
     * 2. Append '\n' after each element.
     */
    void dump_jsonl_to(std::pmr::string &sink, const Array &arr)
    {
        DumpOptions compact{};
        for (const auto &el : arr)
        {
            // Per-element DumpState mirrors the old "each element throws on
            // its own"; the first failure aborts immediately.
            DumpState st;
            if (!dump_value_to(sink, el, compact, st))
                throw JsonError(st.error);
            sink.push_back('\n');
        }
    }

    std::pmr::string dump_jsonl(const Array &arr, std::pmr::memory_resource *res)
    {
        auto r = dump_jsonl_impl(arr, res);
        if (r.is_err())
            throw std::move(r).unwrap_err();
        return std::move(r).unwrap();
    }

    static pjh::result::Result<std::pmr::string, JsonError>
    dump_jsonl_file_impl(std::string_view path, const Array &arr)
    {
        auto dr = dump_jsonl_impl(arr, Config::instance().resource());
        if (dr.is_err())
            return dr;
        std::pmr::string out = std::move(dr).unwrap();

        DumpState st;
        if (!write_file_impl(path, out, st))
            return pjh::result::Result<std::pmr::string, JsonError>::Err(
                JsonError(st.error));
        return pjh::result::Result<std::pmr::string, JsonError>::Ok(std::move(out));
    }

    void dump_jsonl_file(std::string_view path, const Array &arr)
    {
        auto r = dump_jsonl_file_impl(path, arr);
        if (r.is_err())
            throw std::move(r).unwrap_err();
        (void)std::move(r).unwrap();
    }

    /*
     * *_result entries are the primitive form: each forwards straight to its
     * *_impl. No catch anywhere; std::bad_alloc escapes unconverted.
     */

    pjh::result::Result<std::pmr::string, JsonError> dump_jsonl_result(
        const Array &arr, std::pmr::memory_resource *res)
    {
        return dump_jsonl_impl(arr, res);
    }

    pjh::result::Result<std::pmr::string, JsonError> dump_jsonl_file_result(
        std::string_view path, const Array &arr)
    {
        return dump_jsonl_file_impl(path, arr);
    }
}

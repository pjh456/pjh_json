#include "pjh_json/writer.hpp"
#include "pjh_json/detail/writer_state.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <ostream>

namespace pjh::json
{
    /*
     * Write newline + indentation for pretty-print.
     */
    static void write_indent(std::pmr::string &sink, const DumpOptions &opts, size_t depth)
    {
        sink.push_back('\n');
        sink.append(static_cast<size_t>(opts.indent) * depth, opts.indent_char);
    }

    /*
     * Serialise double to JSON string
     *
     * 1. Reject NaN/Inf (not valid JSON).
     * 2. Format via std::to_chars (shortest round-trip representation).
     * 3. If the output is a bare integer (no '.' or 'e'), append ".0"
     *    to distinguish float from int64 on round-trip.
     */
    [[nodiscard]] static bool write_double(std::pmr::string &sink, double val, DumpState &st)
    {
        if (!std::isfinite(val))
        {
            st.fail(ErrorCode::NonFiniteDouble);
            return false;
        }

        char buf[32];
        auto [end, ec] = std::to_chars(buf, buf + sizeof(buf), val);
        if (ec != std::errc{})
        {
            st.fail(ErrorCode::FormatDoubleFailed);
            return false;
        }

        sink.append(buf, end - buf);

        // Preserve float-ness: append ".0" if to_chars emitted a bare integer
        bool has_point = false;
        for (const char *p = buf; p < end; ++p)
        {
            if (*p == '.' || *p == 'e' || *p == 'E')
            {
                has_point = true;
                break;
            }
        }
        if (!has_point)
            sink.append(".0");
        return true;
    }

    /*
     * Recursively write a Json value into sink
     *
     * 1. Dispatch by variant type: null, bool, int64, double, string.
     * 2. Array: '[' + elements (comma-separated, indented if pretty) + ']'.
     * 3. Object: '{' + key:value pairs + '}'.
     *    - If sort_keys: collect entry pointers, sort by key, emit sorted.
     *    - Otherwise emit in insertion order.
     * Container branches reject depth + 1 > max_depth (0 = unlimited)
     * before the empty-container early return, so empty containers count.
     */
    [[nodiscard]] static bool write_value(std::pmr::string &sink, const Json &value,
                                          const DumpOptions &opts, size_t depth, size_t max_depth,
                                          DumpState &st)
    {
        if (value.is_null())
        {
            sink.append("null");
        }
        else if (auto b = value.try_as_boolean())
        {
            sink.append(*b ? "true" : "false");
        }
        else if (auto i = value.try_as_int())
        {
            char buf[24];
            auto [end, ec] = std::to_chars(buf, buf + sizeof(buf), *i);
            if (ec != std::errc{})
            {
                st.fail(ErrorCode::FormatIntFailed);
                return false;
            }
            sink.append(buf, end - buf);
        }
        else if (auto f = value.try_as_float())
        {
            if (!write_double(sink, *f, st))
                return false;
        }
        else if (auto s = value.try_as_string())
        {
            if (!write_escaped_impl(sink, *s, opts.ascii, st))
                return false;
        }
        else if (auto *arr = value.try_as_array())
        {
            if (max_depth != 0 && depth + 1 > max_depth)
            {
                st.fail(ErrorCode::DumpMaxDepthExceeded);
                return false;
            }
            if (arr->empty())
            {
                sink.append("[]");
                return true;
            }
            sink.push_back('[');
            bool first = true;
            for (const auto &el : *arr)
            {
                if (!first)
                    sink.push_back(',');
                first = false;
                if (opts.pretty)
                    write_indent(sink, opts, depth + 1);
                if (!write_value(sink, el, opts, depth + 1, max_depth, st))
                    return false;
            }
            if (opts.pretty)
                write_indent(sink, opts, depth);
            sink.push_back(']');
        }
        else if (auto *obj = value.try_as_object())
        {
            if (max_depth != 0 && depth + 1 > max_depth)
            {
                st.fail(ErrorCode::DumpMaxDepthExceeded);
                return false;
            }
            if (obj->empty())
            {
                sink.append("{}");
                return true;
            }
            sink.push_back('{');
            bool first = true;

            if (opts.sort_keys)
            {
                // Build pointer vector, sort by key, emit in sorted order.
                // data() (not *obj): the iterator yields a per-step EntryRef
                // proxy, so &e must alias the stored entry (task 21.1).
                std::pmr::vector<const Object::Entry *> sorted(obj->data().get_allocator());
                sorted.reserve(obj->size());
                for (const auto &e : obj->data())
                    sorted.push_back(&e);
                std::sort(sorted.begin(), sorted.end(),
                          [](const Object::Entry *a, const Object::Entry *b)
                          { return static_cast<std::string_view>(a->first) < static_cast<std::string_view>(b->first); });
                for (const auto *e : sorted)
                {
                    if (!first)
                        sink.push_back(',');
                    first = false;
                    if (opts.pretty)
                        write_indent(sink, opts, depth + 1);
                    if (!write_escaped_impl(sink, e->first, opts.ascii, st))
                        return false;
                    sink.append(opts.pretty ? ": " : ":");
                    if (!write_value(sink, e->second, opts, depth + 1, max_depth, st))
                        return false;
                }
            }
            else
            {
                // Emit in insertion order (default)
                for (const auto &[key, val] : *obj)
                {
                    if (!first)
                        sink.push_back(',');
                    first = false;
                    if (opts.pretty)
                        write_indent(sink, opts, depth + 1);
                    if (!write_escaped_impl(sink, key, opts.ascii, st))
                        return false;
                    sink.append(opts.pretty ? ": " : ":");
                    if (!write_value(sink, val, opts, depth + 1, max_depth, st))
                        return false;
                }
            }
            if (opts.pretty)
                write_indent(sink, opts, depth);
            sink.push_back('}');
        }
        return true;
    }

    /*
     * Serialize JSON value into a pmr::string sink at depth 0.
     * Shared kernel driver for dump_to(Pmr)/dump/dump_jsonl_to.
     */
    [[nodiscard]] bool dump_value_to(std::pmr::string &sink, const Json &value,
                                     const DumpOptions &opts, DumpState &st)
    {
        return write_value(sink, value, opts, 0, Config::instance().max_depth(), st);
    }

    /*
     * Serialize JSON value into a pmr::string sink
     */
    void dump_to(std::pmr::string &sink, const Json &value, const DumpOptions &opts)
    {
        DumpState st;
        if (!dump_value_to(sink, value, opts, st))
            throw JsonError(st.error); // pmr sink may be partially written (documented)
    }

    /*
     * Serialize JSON value into an existing std::string sink
     *
     * 1. Materialize the value into a pmr::string (same core as dump).
     * 2. Append the result to the caller's std::string (never cleared).
     * The materialization precedes any sink mutation, so a serialization
     * throw leaves the sink byte-identical (the pmr::string sink, which
     * writes directly through write_value, can be left partially written).
     */
    void dump_to(std::string &sink, const Json &value, const DumpOptions &opts)
    {
        std::pmr::string out = dump(value, opts);
        sink.append(out.data(), out.size());
    }

    /*
     * Serialize JSON value into a std::ostream
     */
    void dump_to(std::ostream &os, const Json &value, const DumpOptions &opts)
    {
        std::pmr::string out = dump(value, opts);
        os.write(out.data(), static_cast<std::streamsize>(out.size()));
        if (!os)
            throw JsonError(Error{ErrorCode::StreamWriteFailed,
                                  Category::Json, 0, false, {}});
    }

    /*
     * Serialize JSON value into a new pmr::string
     *
     * 1. Allocate sink string from the given resource.
     * 2. Recursively write the value tree.
     */
    std::pmr::string dump(const Json &value, const DumpOptions &opts,
                          std::pmr::memory_resource *res)
    {
        DumpState st;
        std::pmr::string sink(res);
        if (!dump_value_to(sink, value, opts, st))
            throw JsonError(st.error);
        return sink;
    }

    /*
     * Serialize Document (delegates to Json dump on root value)
     */
    std::pmr::string dump(const Document &doc, const DumpOptions &opts,
                          std::pmr::memory_resource *res)
    {
        return dump(doc.root(), opts, res);
    }

    /*
     * Write raw data to file (binary mode)
     */
    void write_file(std::string_view path, std::string_view data)
    {
        std::ofstream file(std::string(path), std::ios::binary);
        if (!file.is_open())
            throw JsonError(Error{ErrorCode::FileWriteOpenFailed,
                                  Category::Json, 0, false, path});
        file.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!file)
            throw JsonError(Error{ErrorCode::FileWriteFailed,
                                  Category::Json, 0, false, path});
        file.close();
        if (!file)
            throw JsonError(Error{ErrorCode::FileWriteCloseFailed,
                                  Category::Json, 0, false, path});
    }

    /*
     * Serialize JSON value and write to file
     *
     * 1. Dump into a pmr::string.
     * 2. Write the serialized content to disk.
     */
    void dump_file(std::string_view path, const Json &value, const DumpOptions &opts)
    {
        std::pmr::string out = dump(value, opts);
        write_file(path, out);
    }

    /*
     * Prettify: parse then dump with pretty=true
     */
    std::pmr::string prettify(std::string_view json, const DumpOptions &opts,
                              std::pmr::memory_resource *res)
    {
        Document doc = parse_copy(json);
        return dump(doc.root(), opts, res);
    }

    /*
     * Structured-entry shells (task 16): thin catch-and-wrap over the
     * throwing writer entries. Every writer throw site is the base JsonError
     * (no more-derived class exists on this side), so the single-cell ladder
     * is complete: catch by value into the Result channel, nothing to
     * rethrow (non-JsonError exceptions such as std::bad_alloc escape).
     */

    pjh::result::Result<std::pmr::string, JsonError> dump_result(
        const Json &value, const DumpOptions &opts, std::pmr::memory_resource *res)
    {
        try
        {
            return pjh::result::Result<std::pmr::string, JsonError>::Ok(dump(value, opts, res));
        }
        catch (const JsonError &e)
        {
            return pjh::result::Result<std::pmr::string, JsonError>::Err(JsonError(e));
        }
    }

    pjh::result::Result<std::pmr::string, JsonError> dump_result(
        const Document &doc, const DumpOptions &opts, std::pmr::memory_resource *res)
    {
        try
        {
            return pjh::result::Result<std::pmr::string, JsonError>::Ok(dump(doc, opts, res));
        }
        catch (const JsonError &e)
        {
            return pjh::result::Result<std::pmr::string, JsonError>::Err(JsonError(e));
        }
    }

    pjh::result::Result<std::pmr::string, JsonError> dump_file_result(
        std::string_view path, const Json &value, const DumpOptions &opts)
    {
        try
        {
            std::pmr::string out = dump(value, opts);
            write_file(path, out);
            return pjh::result::Result<std::pmr::string, JsonError>::Ok(std::move(out));
        }
        catch (const JsonError &e)
        {
            return pjh::result::Result<std::pmr::string, JsonError>::Err(JsonError(e));
        }
    }
}

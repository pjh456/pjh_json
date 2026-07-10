#include "pjh_json/writer.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <ostream>

namespace pjh::json
{
    // Write newline + indentation for pretty-print.
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
    static void write_double(std::pmr::string &sink, double val)
    {
        if (!std::isfinite(val))
            throw JsonError("Cannot serialize non-finite double (NaN/Inf) to JSON");

        char buf[32];
        auto [end, ec] = std::to_chars(buf, buf + sizeof(buf), val);
        if (ec != std::errc{})
            throw JsonError("Failed to format double");

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
    }

    /*
     * Recursively write a Json value into sink
     *
     * 1. Dispatch by variant type: null, bool, int64, double, string.
     * 2. Array: '[' + elements (comma-separated, indented if pretty) + ']'.
     * 3. Object: '{' + key:value pairs + '}'.
     *    - If sort_keys: collect entry pointers, sort by key, emit sorted.
     *    - Otherwise emit in insertion order.
     */
    static void write_value(std::pmr::string &sink, const Json &value,
                            const DumpOptions &opts, size_t depth)
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
            sink.append(buf, end - buf);
        }
        else if (auto f = value.try_as_float())
        {
            write_double(sink, *f);
        }
        else if (auto s = value.try_as_string())
        {
            write_escaped(sink, *s, opts.ascii);
        }
        else if (auto *arr = value.try_as_array())
        {
            if (arr->empty())
            {
                sink.append("[]");
                return;
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
                write_value(sink, el, opts, depth + 1);
            }
            if (opts.pretty)
                write_indent(sink, opts, depth);
            sink.push_back(']');
        }
        else if (auto *obj = value.try_as_object())
        {
            if (obj->empty())
            {
                sink.append("{}");
                return;
            }
            sink.push_back('{');
            bool first = true;

            if (opts.sort_keys)
            {
                // Build pointer vector, sort by key, emit in sorted order
                std::pmr::vector<const Object::Entry *> sorted(obj->data().get_allocator());
                sorted.reserve(obj->size());
                for (const auto &e : *obj)
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
                    write_escaped(sink, e->first, opts.ascii);
                    sink.append(opts.pretty ? ": " : ":");
                    write_value(sink, e->second, opts, depth + 1);
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
                    write_escaped(sink, key, opts.ascii);
                    sink.append(opts.pretty ? ": " : ":");
                    write_value(sink, val, opts, depth + 1);
                }
            }
            if (opts.pretty)
                write_indent(sink, opts, depth);
            sink.push_back('}');
        }
    }

    void dump_to(std::pmr::string &sink, const Json &value, const DumpOptions &opts)
    {
        write_value(sink, value, opts, 0);
    }

    void dump_to(std::ostream &os, const Json &value, const DumpOptions &opts)
    {
        std::pmr::string out = dump(value, opts);
        os.write(out.data(), static_cast<std::streamsize>(out.size()));
    }

    std::pmr::string dump(const Json &value, const DumpOptions &opts,
                          std::pmr::memory_resource *res)
    {
        std::pmr::string sink(res);
        write_value(sink, value, opts, 0);
        return sink;
    }

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
            throw JsonError("Failed to open file for writing: " + std::string(path));
        file.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!file)
            throw JsonError("Failed to write file: " + std::string(path));
    }

    void dump_file(std::string_view path, const Json &value, const DumpOptions &opts)
    {
        std::pmr::string out = dump(value, opts);
        write_file(path, out);
    }

    /*
     * Prettify: parse then dump with pretty=true
     */
    std::pmr::string prettify(std::string_view json, uint8_t indent,
                              std::pmr::memory_resource *res)
    {
        Document doc = parse_copy(json);
        return dump(doc.root(), DumpOptions{.pretty = true, .indent = indent}, res);
    }
}

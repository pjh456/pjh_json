#include "pjh_json/writer.hpp"

namespace pjh::json
{
    void dump_jsonl_to(std::pmr::string &sink, const Array &arr)
    {
        DumpOptions compact{};
        for (const auto &el : arr)
        {
            dump_to(sink, el, compact);
            sink.push_back('\n');
        }
    }

    std::pmr::string dump_jsonl(const Array &arr, std::pmr::memory_resource *res)
    {
        std::pmr::string sink(res);
        dump_jsonl_to(sink, arr);
        return sink;
    }

    void dump_jsonl_file(std::string_view path, const Array &arr)
    {
        write_file(path, dump_jsonl(arr));
    }
}

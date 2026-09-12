// libFuzzer entry: parse_jsonl differential oracle (see fuzz_oracle.hpp).
#include "fuzz_oracle.hpp"

extern "C" int LLVMFuzzerInitialize(int * /*argc*/, char *** /*argv*/)
{
    // Proves the oracle can both agree and detect a real divergence.
    pjh::json::fuzz::run_oracle_selftest();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    const std::string_view in(reinterpret_cast<const char *>(data), size);
    if (!pjh::json::fuzz::oracle_parse_jsonl(in))
        pjh::json::fuzz::fuzz_fail("parse_jsonl: pjh/nlohmann divergence");
    return 0;
}

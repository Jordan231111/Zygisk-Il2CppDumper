#pragma once
#include <string>
namespace dumper {
struct DumpOptions {
    unsigned timeout_seconds{120};
    bool verbose{};
    std::string process;
};
bool dump_runtime(void* handle, const std::string& data_directory, const DumpOptions& options);
} // namespace dumper

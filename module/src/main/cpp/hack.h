#pragma once
#include "il2cpp_dump.h"
#include <cstddef>
#include <jni.h>
#include <memory>
#include <string>
#include <vector>
namespace dumper {
struct WorkerContext {
    JavaVM* vm{};
    std::string data_directory;
    DumpOptions options;
    std::vector<std::byte> bridge_payload;
};
bool start_worker(std::unique_ptr<WorkerContext> context);
} // namespace dumper

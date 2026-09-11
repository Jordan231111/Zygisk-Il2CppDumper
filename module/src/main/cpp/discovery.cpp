#include "discovery.h"
#include "core/elf.h"
#include "core/maps.h"
#include "log.h"
#include "xdl.h"
#include <string>
#include <unordered_set>
#include <vector>
namespace dumper {
namespace {
struct Candidate {
    uintptr_t base;
    std::string name;
};
int collect(dl_phdr_info* info, size_t, void* opaque) {
    if (info && info->dlpi_addr && info->dlpi_name) {
        static_cast<std::vector<Candidate>*>(opaque)->push_back({info->dlpi_addr, info->dlpi_name});
    }
    return 0;
}
uint16_t native_machine() {
#if defined(__aarch64__)
    return 183;
#elif defined(__arm__)
    return 40;
#elif defined(__x86_64__)
    return 62;
#else
    return 3;
#endif
}
void* inspect(uintptr_t address, const std::string& name, const Maps& maps, const Memory& memory,
              const char* strategy) {
    const auto read = [&](uintptr_t at, void* destination, size_t size) {
        return maps.readable(at, size) && memory.read(at, destination, size);
    };
    std::string error;
    const auto image = elf::image_from_memory(address, read, error);
    if (!image || image->machine != native_machine() || image->is64 != (sizeof(uintptr_t) == 8))
        return nullptr;
    std::unordered_map<std::string, uintptr_t> symbols;
    if (!elf::dynamic_symbols(*image, read, symbols, error))
        return nullptr;
    for (const char* anchor :
         {"il2cpp_get_corlib", "il2cpp_domain_get", "il2cpp_class_get_methods"}) {
        const auto found = symbols.find(anchor);
        if (found == symbols.end() || !maps.executable(found->second))
            return nullptr;
    }
    dl_phdr_info info{};
    info.dlpi_addr = image->load_bias;
    info.dlpi_name = name.c_str();
    info.dlpi_phdr = reinterpret_cast<const ElfW(Phdr)*>(image->program_headers);
    info.dlpi_phnum = static_cast<ElfW(Half)>(image->program_header_count);
    auto* handle = xdl_open2(&info);
    if (handle)
        LOGI("stage=discover strategy=%s module=%s load_bias=%p", strategy, name.c_str(),
             reinterpret_cast<void*>(image->load_bias));
    return handle;
}
} // namespace
void* discover_il2cpp(bool include_fallbacks, bool use_loader) {
    if (use_loader) {
        if (auto* handle = xdl_open("libil2cpp.so", XDL_DEFAULT))
            return handle;
    }
    if (!include_fallbacks)
        return nullptr;
    const auto maps = Maps::read_self();
    Memory memory;
    std::vector<Candidate> candidates;
    if (use_loader)
        xdl_iterate_phdr(collect, &candidates, XDL_DEFAULT);
    std::unordered_set<uintptr_t> inspected;
    for (const auto& candidate : candidates) {
        inspected.insert(candidate.base);
        if (auto* handle =
                inspect(candidate.base, candidate.name, maps, memory, "renamed-loader-module"))
            return handle;
    }
    for (const auto& mapping : maps.entries()) {
        if (!mapping.readable || !inspected.insert(mapping.begin).second)
            continue;
        const auto name = mapping.path.empty() ? "[mapped-il2cpp]" : mapping.path;
        if (auto* handle = inspect(mapping.begin, name, maps, memory, "mapped-ELF"))
            return handle;
    }
    return nullptr;
}
} // namespace dumper

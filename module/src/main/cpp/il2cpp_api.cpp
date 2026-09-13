#include "il2cpp_api.h"
#include "core/maps.h"
#include "log.h"
#include "xdl.h"
#include <array>
#include <cerrno>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <unistd.h>
namespace dumper {
namespace {
std::unordered_map<std::string, uintptr_t> local_symbols(const char* path, const elf::Image& image,
                                                         const Memory& memory) {
    std::unordered_map<std::string, uintptr_t> result;
    if (!path || path[0] != '/' || std::strstr(path, "!/"))
        return result;
    const int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return result;
    struct File {
        int fd;
        ~File() { close(fd); }
    } file{fd};
    struct stat status{};
    if (fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) || status.st_size < 64 ||
        status.st_size > 512 * 1024 * 1024)
        return result;
    std::vector<std::byte> bytes(static_cast<size_t>(status.st_size));
    size_t done = 0;
    while (done < bytes.size()) {
        const auto count =
            pread(fd, bytes.data() + done, bytes.size() - done, static_cast<off_t>(done));
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return result;
        done += static_cast<size_t>(count);
    }
    std::string error;
    const auto symbols = elf::file_symbols(bytes, error);
    if (!error.empty()) {
        LOGW("strategy=file-symbols reason=%s", error.c_str());
        return result;
    }
    for (const auto& [name, value] : symbols) {
        if (!name.starts_with("il2cpp_") || value > std::numeric_limits<uintptr_t>::max())
            continue;
        const auto function = checked_add(image.load_bias, static_cast<uintptr_t>(value));
        if (!function)
            continue;
        const auto code = *function & (image.machine == 40 ? ~uintptr_t{1} : ~uintptr_t{0});
        if (!image.contains(code, 16, elf::kExecute))
            continue;
        // A file may have been replaced since loading. Verify that its function
        // bytes match the resident image before using a file-only symbol.
        for (const auto& segment : image.segments) {
            const auto rva = code - image.load_bias;
            if (segment.type != elf::kLoad || rva < segment.vaddr ||
                rva - segment.vaddr > segment.filesz || segment.filesz - (rva - segment.vaddr) < 16)
                continue;
            const uint64_t delta = rva - segment.vaddr;
            if (segment.offset > bytes.size() || delta > bytes.size() - segment.offset)
                continue;
            const size_t offset = static_cast<size_t>(segment.offset + delta);
            std::array<std::byte, 16> resident{};
            if (bounded(bytes.size(), offset, resident.size()) &&
                memory.read(code, resident.data(), resident.size()) &&
                std::memcmp(resident.data(), bytes.data() + offset, resident.size()) == 0)
                result.emplace(name, *function);
            break;
        }
    }
    return result;
}
} // namespace
Il2CppApi::~Il2CppApi() {
    if (native_handle_)
        dlclose(native_handle_);
}
bool Il2CppApi::can_enumerate() const {
    return (image_get_class && image_get_class_count) ||
           (class_from_name && class_get_method_from_name && runtime_invoke &&
            class_from_system_type && string_new && object_get_class && object_unbox);
}
bool Il2CppApi::can_calibrate_methods() const {
    return class_from_name && class_get_field_from_name && field_static_get_value &&
           field_get_value && object_get_class && runtime_class_init;
}
bool Il2CppApi::load(void* handle, std::string& error) {
    error.clear();
    if (native_handle_) {
        dlclose(native_handle_);
        native_handle_ = nullptr;
    }
#define API(result, name, parameters, required) name = nullptr;
#include "il2cpp-api-functions.h"
#undef API
    module_image = {};
    const auto maps = Maps::read_self();
    Memory memory;
    xdl_info_t info{};
    if (xdl_info(handle, XDL_DI_DLINFO, &info) != 0 || info.dlpi_phnum == 0 ||
        info.dlpi_phnum > 4096 || !info.dlpi_phdr) {
        error = "invalid loader program headers";
        return false;
    }
    module_image.load_bias = reinterpret_cast<uintptr_t>(info.dli_fbase);
    module_image.is64 = sizeof(uintptr_t) == 8;
#if defined(__aarch64__)
    module_image.machine = 183;
#elif defined(__arm__)
    module_image.machine = 40;
#elif defined(__x86_64__)
    module_image.machine = 62;
#else
    module_image.machine = 3;
#endif
    module_image.segments.clear();
    for (size_t i = 0; i < info.dlpi_phnum; ++i) {
        ElfW(Phdr) program{};
        const auto address =
            checked_add(reinterpret_cast<uintptr_t>(info.dlpi_phdr), i * sizeof(program));
        if (!address || !memory.read(*address, &program, sizeof(program)) ||
            program.p_memsz < program.p_filesz ||
            program.p_vaddr > std::numeric_limits<uintptr_t>::max() - module_image.load_bias ||
            program.p_memsz >
                std::numeric_limits<uintptr_t>::max() - module_image.load_bias - program.p_vaddr) {
            error = "unreadable/invalid program header";
            return false;
        }
        module_image.segments.push_back({program.p_type, program.p_flags, program.p_offset,
                                         program.p_vaddr, program.p_filesz, program.p_memsz});
    }
    if (info.dli_fname)
        native_handle_ = dlopen(info.dli_fname, RTLD_NOW | RTLD_NOLOAD);
    std::unordered_map<std::string, uintptr_t> dynamic;
    std::string parse_error;
    elf::dynamic_symbols(
        module_image,
        [&](uintptr_t address, void* target, size_t size) {
            return maps.readable(address, size) && memory.read(address, target, size);
        },
        dynamic, parse_error);
    if (!parse_error.empty())
        LOGW("strategy=bounded-ELF reason=%s", parse_error.c_str());
    size_t native_count = 0, dynamic_count = 0, file_count = 0;
    auto resolve = [&](const char* name) -> void* {
        if (native_handle_) {
            auto* symbol = dlsym(native_handle_, name);
            if (symbol && maps.executable(reinterpret_cast<uintptr_t>(symbol))) {
                ++native_count;
                return symbol;
            }
        }
        const auto it = dynamic.find(name);
        if (it != dynamic.end() && maps.executable(it->second)) {
            ++dynamic_count;
            return reinterpret_cast<void*>(it->second);
        }
        return nullptr;
    };
    bool needs_file = false;
#define API(result, name, parameters, required)                                                    \
    name = reinterpret_cast<decltype(name)>(resolve("il2cpp_" #name));                             \
    if (required && !name)                                                                         \
        needs_file = true;
#include "il2cpp-api-functions.h"
#undef API
    // Optional exports can form a required capability group. Recover those
    // groups before deciding that enumeration or native addresses are unavailable.
    needs_file |= !can_enumerate() || !can_calibrate_methods();
    if (needs_file) {
        const auto symbols = local_symbols(info.dli_fname, module_image, memory);
#define API(result, name, parameters, required)                                                    \
    if (!name) {                                                                                   \
        const auto it = symbols.find("il2cpp_" #name);                                             \
        if (it != symbols.end() && maps.executable(it->second)) {                                  \
            name = reinterpret_cast<decltype(name)>(it->second);                                   \
            ++file_count;                                                                          \
        }                                                                                          \
    }
#include "il2cpp-api-functions.h"
#undef API
    }
#define API(result, name, parameters, required)                                                    \
    if (required && !name) {                                                                       \
        if (!error.empty())                                                                        \
            error += ", ";                                                                         \
        error += "il2cpp_" #name;                                                                  \
    }
#include "il2cpp-api-functions.h"
#undef API
    LOGI("stage=resolve dlsym=%zu bounded_ELF=%zu file_symbols=%zu missing_required=%s",
         native_count, dynamic_count, file_count, error.empty() ? "none" : error.c_str());
    if (!error.empty())
        return false;
    if (!can_enumerate()) {
        error = "neither image enumeration nor managed reflection is available";
        return false;
    }
    return true;
}
} // namespace dumper

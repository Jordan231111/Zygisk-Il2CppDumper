#include "discovery.h"
#include "il2cpp_dump.h"
#include "xdl.h"
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unistd.h>
int main(int argc, char** argv) {
    if (argc < 3)
        return 2;
    void* library = dlopen(argv[1], RTLD_NOW);
    if (!library) {
        std::cerr << dlerror() << '\n';
        return 2;
    }
    auto configure = reinterpret_cast<void (*)(int)>(dlsym(library, "fixture_configure"));
    auto attached = reinterpret_cast<int (*)()>(dlsym(library, "fixture_attached"));
    auto method_address =
        reinterpret_cast<uintptr_t (*)()>(dlsym(library, "fixture_method_address"));
    if (!configure || !attached || !method_address)
        return 2;
    const int mode = std::stoi(argv[2]);
    configure(mode);
    void* handle = dumper::discover_il2cpp(true, argc < 4);
    if (!handle) {
        std::cerr << "discovery failed\n";
        return 1;
    }
    auto root =
        std::filesystem::temp_directory_path() / ("il2cpp-runtime-" + std::to_string(getpid()));
    std::filesystem::create_directories(root / "files");
    {
        std::ofstream previous(root / "files/dump.cs");
        previous << "previous";
    }
    const bool result = dumper::dump_runtime(handle, root.string(),
                                             dumper::DumpOptions{1, false, "test.il2cpp.fixture"});
    std::ifstream input(root / "files/dump.cs");
    const std::string text(std::istreambuf_iterator<char>(input), {});
    bool valid = attached() == 0;
    if (mode == 0 || mode == 6 || mode == 8) {
        valid &= result && text.find("out System.Int32 result") != std::string::npos &&
                 text.find("= -1;") != std::string::npos &&
                 text.find("System.String Name { get; set; }") != std::string::npos &&
                 text.find("<invalid-name>") == std::string::npos;
        xdl_info_t info{};
        valid &= xdl_info(handle, XDL_DI_DLINFO, &info) == 0;
        std::ostringstream expected;
        if (mode == 6)
            expected << "// RVA: unavailable VA: 0x0";
        else
            expected << "// RVA: 0x" << std::hex
                     << (method_address() - reinterpret_cast<uintptr_t>(info.dli_fbase))
                     << " VA: 0x" << method_address();
        size_t count = 0, offset = 0;
        while ((offset = text.find(expected.str(), offset)) != std::string::npos) {
            ++count;
            ++offset;
        }
        valid &= count == 3;
    } else
        valid &= !result && text == "previous";
    std::filesystem::remove_all(root);
    xdl_close(handle);
    dlclose(library);
    std::cout << (valid ? "runtime test passed" : "runtime test FAILED") << '\n';
    return valid ? 0 : 1;
}

#include "discovery.h"
#include "il2cpp_dump.h"
#include "xdl.h"
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
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
    if (!configure || !attached)
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
    if (mode == 0)
        valid &= result && text.find("out System.Int32 result") != std::string::npos &&
                 text.find("= -1;") != std::string::npos &&
                 text.find("System.String Name { get; set; }") != std::string::npos &&
                 text.find("<invalid-name>") == std::string::npos;
    else
        valid &= !result && text == "previous";
    std::filesystem::remove_all(root);
    xdl_close(handle);
    dlclose(library);
    std::cout << (valid ? "runtime test passed" : "runtime test FAILED") << '\n';
    return valid ? 0 : 1;
}

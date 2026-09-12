#include "core/output.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <sys/resource.h>
#include <unistd.h>
#include <vector>
int main(int argc, char** argv) {
    if (argc != 2 || (std::string(argv[1]) != "buffered" && std::string(argv[1]) != "streamed"))
        return 2;
    auto temporary = (std::filesystem::temp_directory_path() / "il2cpp-bench-XXXXXX").string();
    const auto* directory = mkdtemp(temporary.data());
    if (!directory)
        return 1;
    const auto started = std::chrono::steady_clock::now();
    dumper::AtomicOutput output(directory);
    const std::string payload =
        "// Synthetic output staging benchmark\n" + std::string(2048, 'x') + "\n";
    constexpr size_t count = 40000;
    if (std::string(argv[1]) == "buffered") {
        std::vector<std::string> retained;
        retained.reserve(count);
        for (size_t i = 0; i < count; ++i)
            retained.push_back(payload);
        for (const auto& value : retained)
            if (!output.write(value))
                return 1;
    } else {
        for (size_t i = 0; i < count; ++i)
            if (!output.write(payload))
                return 1;
    }
    if (!output.commit())
        return 1;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - started)
                             .count();
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
#ifdef __APPLE__
    const auto rss = usage.ru_maxrss / 1024;
#else
    const auto rss = usage.ru_maxrss;
#endif
    std::cout << "{\"mode\":\"" << argv[1] << "\",\"bytes\":" << output.bytes()
              << ",\"milliseconds\":" << elapsed << ",\"peak_rss_kib\":" << rss << "}\n";
    std::filesystem::remove_all(directory);
}

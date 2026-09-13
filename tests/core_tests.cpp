#include "core/binary.h"
#include "core/maps.h"
#include "core/output.h"
#include "core/runtime_layout.h"
#include "core/target.h"
#include <array>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>

namespace {
size_t checks{};
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(condition)) {                                                                        \
            std::cerr << "FAIL line " << __LINE__ << ": " #condition "\n";                         \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)
std::string read_file(const std::filesystem::path& file) {
    std::ifstream input(file);
    return std::string(std::istreambuf_iterator<char>(input), {});
}
} // namespace
int main() {
    using namespace dumper;
    const auto maps =
        Maps::parse("1000-2000 r--p 00004000 fe:02 11 /data/app/a b/base.apk!lib.so (deleted)\n"
                    "2000-6000 r-xp 00008000 fe:02 11 /data/app/a b/base.apk!lib.so (deleted)\n"
                    "6000-7000 ---p 00000000 00:00 0\n"
                    "8000-c000 rw-s 00000000 00:00 0 [anon:metadata]\n");
    CHECK(maps.entries().size() == 4);
    CHECK(maps.entries()[0].file_offset == 0x4000);
    CHECK(maps.entries()[0].path == "/data/app/a b/base.apk!lib.so (deleted)");
    CHECK(maps.readable(0x1fff, 2));
    CHECK(!maps.readable(0x5fff, 2));
    CHECK(!maps.readable(0x7000, 1));
    CHECK(maps.executable(0x4000));
    CHECK(!maps.executable(0x1000));
    CHECK(maps.find(0xc000) == nullptr);
    for (auto text :
         {"", "bogus", "2000-1000 r-xp 0 00:00 0", "0-0 r-xp 0 00:00 0", "1-2 rwxp ? 00:00 0",
          "1-2 rwx! 0 00:00 0", "1-2 rwxp 0 invalid 0", "1-2 rwxp 0 00:00 x", "-1-2 rwxp 0 00:00 0",
          "10000000000000000-2 rwxp 0 00:00 0"})
        CHECK(!parse_mapping(text));
    CHECK(range_contains(1, 9, 8, 1));
    CHECK(!range_contains(1, 9, 8, 2));
    CHECK(!range_contains(1, 9, std::numeric_limits<uintptr_t>::max(), 4));
    CHECK(!range_contains(9, 1, 1, 0));
    CHECK(bounded(4, 4, 0));
    CHECK(!bounded(4, 3, 2));
    CHECK(!bounded(4, std::numeric_limits<size_t>::max(), 2));
    CHECK(!checked_add(std::numeric_limits<uintptr_t>::max(), 1));
    CHECK(checked_add(2, 3) == 5);
    for (auto value : {"com.example.app", "a.b", "com.Example_app2"})
        CHECK(valid_package(value));
    for (auto value :
         {"", ".a", "a.", "a..b", "com.2bad", "com.app;id", "com.a/b", "com.app:child"})
        CHECK(!valid_package(value));
    CHECK(matches_target("com.example.app", "com.example.app\n"));
    CHECK(!matches_target("com.example.app:child", "com.example.app"));
    CHECK(matches_target("com.example.app:child", "com.example.app:*"));
    CHECK(matches_target("com.example.app", "com.example.app:*"));
    CHECK(!matches_target("com.example.app2:child", "com.example.app:*"));
    CHECK(!matches_target("com.example.app:", "com.example.app:*"));
    CHECK(matches_target("com.example.app:child", "# comment\r\ncom.example.app:child\r\n"));
    CHECK(!matches_target("system_server", "system_server"));
    CHECK(!matches_target("com.example.app", std::string(4097, 'a')));
    for (uint32_t version : {24U, 27U, 29U, 31U, 39U, 104U, 106U}) {
        const MetadataHeader header{0xfab11bafU, version};
        const auto bytes = std::as_bytes(std::span(&header, 1));
        CHECK(metadata_header(BinaryView(bytes))->version == version);
        CHECK(!metadata_header(BinaryView(bytes.first(7))));
    }
    const MetadataHeader bad{0, 39};
    CHECK(!metadata_header(BinaryView(std::as_bytes(std::span(&bad, 1)))));
    std::array<std::byte, 12> unaligned{};
    const uint32_t sentinel = 0x12345678;
    std::memcpy(unaligned.data() + 1, &sentinel, sizeof(sentinel));
    CHECK(BinaryView(unaligned).at<uint32_t>(1) == sentinel);
    CHECK(!BinaryView(unaligned).at<uint64_t>(8));
    const uintptr_t code_base = 0x100000;
    std::array<uint32_t, 4> getter{0xf0000008U, 0xf9410100U, 0xb5000040U, 0xd65f03c0U};
    auto read_code = [&](uintptr_t address, void* target, size_t size) {
        if (!range_contains(code_base, code_base + sizeof(getter), address, size))
            return false;
        std::memcpy(target, reinterpret_cast<const std::byte*>(getter.data()) + address - code_base,
                    size);
        return true;
    };
    CHECK(aarch64_lazy_global(code_base, read_code) == 0x103200);
    getter[2] = 0xb5000041U; // A branch on x1 cannot validate a load of x0.
    CHECK(!aarch64_lazy_global(code_base, read_code));
    getter[0] = 0x14000000U; // Cycle: bounded branch following must terminate.
    CHECK(!aarch64_lazy_global(code_base, read_code));
    CHECK(!aarch64_lazy_global(code_base + 1, read_code));
    std::array<uint32_t, 6> predicate{0xf0000008U, 0xf9410108U, 0xf9400908U,
                                      0xeb00011fU, 0x1a9f17e0U, 0xd65f03c0U};
    const auto read_predicate = [&](uintptr_t address, void* target, size_t size) {
        if (!range_contains(code_base, code_base + sizeof(predicate), address, size))
            return false;
        std::memcpy(target,
                    reinterpret_cast<const std::byte*>(predicate.data()) + address - code_base,
                    size);
        return true;
    };
    const auto chain = aarch64_vm_thread_chain(code_base, read_predicate);
    CHECK(chain && chain->global == 0x103200 && chain->offset == 16);
    predicate[3] ^= 1;
    CHECK(!aarch64_vm_thread_chain(code_base, read_predicate));
    predicate[0] = 0x14000000U;
    CHECK(!aarch64_vm_thread_chain(code_base, read_predicate));
    auto temporary = (std::filesystem::temp_directory_path() / "il2cpp-tests-XXXXXX").string();
    auto* dir = mkdtemp(temporary.data());
    CHECK(dir);
    const std::filesystem::path root(dir);
    {
        std::ofstream old(root / "dump.cs");
        old << "previous";
    }
    {
        AtomicOutput output(dir);
        CHECK(output.good());
        CHECK(output.write("unfinished"));
    }
    CHECK(read_file(root / "dump.cs") == "previous");
    CHECK(std::distance(std::filesystem::directory_iterator(root),
                        std::filesystem::directory_iterator{}) == 1);
    {
        AtomicOutput output(dir);
        CHECK(output.write("complete"));
        CHECK(output.commit());
    }
    CHECK(read_file(root / "dump.cs") == "complete");
    {
        AtomicOutput output(dir, "../escape");
        CHECK(!output.good());
    }
    {
        AtomicOutput output(dir, std::string("dump.cs\0suffix", 14));
        CHECK(!output.good());
    }
    {
        AtomicOutput output((root / "missing").string());
        CHECK(!output.good());
    }
    std::filesystem::create_directory_symlink(root, root / "symlink");
    {
        AtomicOutput output((root / "symlink").string());
        CHECK(!output.good());
    }
    std::filesystem::remove_all(root);
#if defined(__linux__)
    const auto memory_fds = [] {
        size_t count = 0;
        for (const auto& item : std::filesystem::directory_iterator("/proc/self/fd")) {
            std::error_code error;
            const auto target = std::filesystem::read_symlink(item.path(), error);
            if (!error && target.filename() == "mem")
                ++count;
        }
        return count;
    };
    const auto before_fds = memory_fds();
    {
        Memory unused;
        Memory shared(Memory::Backend::ProcMem);
        CHECK(memory_fds() == before_fds);
        std::atomic<bool> valid{true};
        std::array<std::thread, 4> readers;
        for (auto& reader : readers)
            reader = std::thread([&] {
                for (unsigned i = 0; i < 1000; ++i)
                    if (shared.read<uint32_t>(reinterpret_cast<uintptr_t>(&sentinel)) != sentinel)
                        valid = false;
            });
        for (auto& reader : readers)
            reader.join();
        CHECK(valid);
        CHECK(memory_fds() == before_fds + 1);
    }
    CHECK(memory_fds() == before_fds);
    Memory memory;
    Memory proc_memory(Memory::Backend::ProcMem);
    CHECK(memory.read<uint32_t>(reinterpret_cast<uintptr_t>(&sentinel)) == sentinel);
    CHECK(proc_memory.read<uint32_t>(reinterpret_cast<uintptr_t>(&sentinel)) == sentinel);
    CHECK(!proc_memory.read<uint32_t>(1));
    CHECK(!memory.read<uint32_t>(0));
    CHECK(!memory.read<uint32_t>(1));
    CHECK(!memory.read<uint32_t>(std::numeric_limits<uintptr_t>::max()));
    const std::array<uintptr_t, 2> pointer_values{0x1234, 0x5678};
    std::array<uintptr_t, 3> addresses{reinterpret_cast<uintptr_t>(&pointer_values[0]), 0,
                                       reinterpret_cast<uintptr_t>(&pointer_values[1])};
    std::array<uintptr_t, 3> received{};
    CHECK(!memory.read_pointers(addresses, received));
    CHECK(received[0] == 0x1234 && received[1] == 0 && received[2] == 0x5678);
    addresses[1] = addresses[0];
    CHECK(memory.read_pointers(addresses, received));
    CHECK(proc_memory.read_pointers(addresses, received));
    CHECK(received[0] == 0x1234 && received[1] == 0x1234 && received[2] == 0x5678);
    CHECK(received[1] == 0x1234);
    CHECK(!memory.read_pointers(addresses, std::span<uintptr_t>(received).first(1)));
    const size_t page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    auto* arena = static_cast<char*>(
        mmap(nullptr, 2 * page_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    CHECK(arena != MAP_FAILED);
    auto stale_maps = Maps::read_self();
    CHECK(mprotect(arena, page_size, PROT_READ | PROT_WRITE) == 0);
    std::memcpy(arena, "newly committed", 16);
    CHECK(memory.string(reinterpret_cast<uintptr_t>(arena), stale_maps) == "newly committed");
    arena[page_size - 1] = 0;
    CHECK(memory.string(reinterpret_cast<uintptr_t>(arena + page_size - 1), stale_maps) == "");
    std::memset(arena, 'x', 8);
    CHECK(!memory.string(reinterpret_cast<uintptr_t>(arena), stale_maps, 8));
    CHECK(munmap(arena, 2 * page_size) == 0);
    CHECK(!proc_memory.read<uint32_t>(reinterpret_cast<uintptr_t>(arena)));
#if UINTPTR_MAX == UINT32_MAX
    // A high-address hint without MAP_FIXED cannot overwrite an existing mapping.
    auto* high =
        static_cast<uint32_t*>(mmap(reinterpret_cast<void*>(0xa0000000U), page_size,
                                    PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    CHECK(high != MAP_FAILED);
    CHECK(reinterpret_cast<uintptr_t>(high) > INT32_MAX);
    *high = sentinel;
    CHECK(proc_memory.read<uint32_t>(reinterpret_cast<uintptr_t>(high)) == sentinel);
    CHECK(munmap(high, page_size) == 0);
#endif
#endif
    std::cout << checks << " checks passed\n";
}

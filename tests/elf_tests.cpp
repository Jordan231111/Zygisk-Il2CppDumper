#include "core/elf.h"
#include "core/maps.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <random>
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
template <class T> void put(std::vector<std::byte>& data, size_t offset, const T& value) {
    CHECK(dumper::bounded(data.size(), offset, sizeof(T)));
    std::memcpy(data.data() + offset, &value, sizeof(T));
}
template <class H, class P, class D, class S, class Sh, class Word> void exercise() {
    using namespace dumper;
    using namespace dumper::elf;
    std::vector<std::byte> data(8192);
    H header{};
    std::memcpy(header.ident, "\177ELF", 4);
    header.ident[4] = sizeof(H) == 64 ? 2 : 1;
    header.ident[5] = header.ident[6] = 1;
    header.type = 3;
    header.machine = sizeof(H) == 64 ? 183 : 40;
    header.version = 1;
    header.ehsize = sizeof(H);
    header.phoff = sizeof(H);
    header.phentsize = sizeof(P);
    header.phnum = 2;
    header.shoff = 4096;
    header.shentsize = sizeof(Sh);
    header.shnum = 3;
    put(data, 0, header);
    P load{};
    load.type = kLoad;
    load.flags = kRead | kExecute;
    load.memsz = load.filesz = 4096;
    load.align = 16384;
    P dynamic{};
    dynamic.type = kDynamic;
    dynamic.offset = dynamic.vaddr = 512;
    dynamic.memsz = dynamic.filesz = 6 * sizeof(D);
    put(data, sizeof(H), load);
    put(data, sizeof(H) + sizeof(P), dynamic);
    std::array<D, 6> entries{{{6, 768}, {11, sizeof(S)}, {5, 896}, {10, 13}, {4, 640}, {0, 0}}};
    put(data, 512, entries);
    const std::array<uint32_t, 5> hash{1, 2, 1, 0, 0};
    put(data, 640, hash);
    S symbol{};
    symbol.name = 1;
    symbol.info = 0x12;
    symbol.section = 1;
    symbol.value = 1280;
    symbol.size = 16;
    put(data, 768 + sizeof(S), symbol);
    const char strings[] = "\0il2cpp_test\0";
    std::memcpy(data.data() + 896, strings, sizeof(strings));
    Sh symbols{};
    symbols.type = 2;
    symbols.offset = 768;
    symbols.size = 2 * sizeof(S);
    symbols.link = 2;
    symbols.entsize = sizeof(S);
    Sh string_section{};
    string_section.type = 3;
    string_section.offset = 896;
    string_section.size = sizeof(strings);
    put(data, 4096 + sizeof(Sh), symbols);
    put(data, 4096 + 2 * sizeof(Sh), string_section);
    const auto original = data;
    constexpr uintptr_t base = 0x100000;
    const auto read = [&](uintptr_t address, void* destination, size_t size) {
        if (!range_contains(base, base + data.size(), address, size))
            return false;
        std::memcpy(destination, data.data() + address - base, size);
        return true;
    };
    auto parse = [&](bool expected) {
        std::string error;
        const auto image = image_from_memory(base, read, error);
        std::unordered_map<std::string, uintptr_t> found;
        const bool success = image && dynamic_symbols(*image, read, found, error);
        CHECK(success == expected);
        if (expected)
            CHECK(found.at("il2cpp_test") == base + 1280);
        else {
            CHECK(!error.empty());
            CHECK(found.empty());
        }
    };
    parse(true);
    std::string error;
    CHECK(file_symbols(data, error).at("il2cpp_test") == 1280);
    // Dynamic lookup works without a section table and with relocated pointers.
    header.shnum = 0;
    put(data, 0, header);
    parse(true);
    CHECK(file_symbols(data, error).empty());
    data = original;
    auto relocated = entries;
    relocated[0].value += base;
    relocated[2].value += base;
    relocated[4].value += base;
    put(data, 512, relocated);
    parse(true);
    data = original;
    auto bad_entries = entries;
    bad_entries[5].tag = 1;
    put(data, 512, bad_entries);
    parse(false);
    data = original;
    bad_entries = entries;
    bad_entries[1].value = 0;
    put(data, 512, bad_entries);
    parse(false);
    data = original;
    bad_entries = entries;
    bad_entries[3].value = 0xffffffffU;
    put(data, 512, bad_entries);
    parse(false);
    data = original;
    bad_entries = entries;
    bad_entries[0].value = 8190;
    put(data, 512, bad_entries);
    parse(false);
    data = original;
    auto bad_symbol = symbol;
    bad_symbol.name = 0xffffffffU;
    put(data, 768 + sizeof(S), bad_symbol);
    parse(false);
    data = original;
    std::fill(data.begin() + 897, data.begin() + 909, std::byte{'x'});
    parse(false);
    data = original;
    std::array<uint32_t, 5> bad_hash{0, 2, 1, 0, 0};
    put(data, 640, bad_hash);
    parse(false);
    data = original;
    // The bounded linear index never follows SYSV chains, including a cycle.
    bad_hash = hash;
    bad_hash[4] = 1;
    put(data, 640, bad_hash);
    parse(true);
    data = original;
    auto gnu = entries;
    gnu[4].tag = 0x6ffffef5;
    put(data, 512, gnu);
    const std::array<uint32_t, 4> gnu_header{1, 1, 1, 6};
    put(data, 640, gnu_header);
    put(data, 656, Word{~Word{0}});
    put(data, 656 + sizeof(Word), uint32_t{1});
    put(data, 660 + sizeof(Word), uint32_t{1});
    parse(true);
    const auto valid_gnu = data;
    put(data, 648, uint32_t{0});
    parse(false);
    data = valid_gnu;
    put(data, 652, uint32_t{32});
    parse(false);
    data = valid_gnu;
    put(data, 656 + sizeof(Word), uint32_t{0xffffffffU});
    parse(false);
    data = valid_gnu;
    // A non-terminated chain must fail at the readable segment boundary.
    std::fill(data.begin() + 660 + sizeof(Word), data.begin() + 4096, std::byte{});
    parse(false);
    data = original;
    auto bad_section = symbols;
    bad_section.entsize = 0;
    put(data, 4096 + sizeof(Sh), bad_section);
    error.clear();
    CHECK(file_symbols(data, error).empty());
    CHECK(!error.empty());
    data = original;
    bad_section = symbols;
    bad_section.link = 3;
    put(data, 4096 + sizeof(Sh), bad_section);
    error.clear();
    CHECK(file_symbols(data, error).empty());
    CHECK(!error.empty());
    data = original;
    auto bad_header = header;
    bad_header.phentsize = 1;
    put(data, 0, bad_header);
    parse(false);
    data = original;
    bad_header = header;
    bad_header.ident[5] = 2;
    put(data, 0, bad_header);
    parse(false);
    data = original;
    bad_header = header;
    bad_header.phoff = std::numeric_limits<decltype(header.phoff)>::max();
    put(data, 0, bad_header);
    parse(false);
    data = original;
    auto bad_load = load;
    bad_load.filesz = 4097;
    put(data, sizeof(H), bad_load);
    parse(false);
    data = original;
    bad_load = load;
    bad_load.align = 123;
    put(data, sizeof(H), bad_load);
    parse(false);
    data = original;
    // Deterministic mutation corpus: no copyrighted binaries or external fixtures.
    std::mt19937 random(17);
    for (unsigned round = 0; round < 1000; ++round) {
        data = original;
        for (unsigned j = 0; j < 8; ++j)
            data[random() % data.size()] = std::byte(random() & 255U);
        std::string diagnostic;
        if (auto image = image_from_memory(base, read, diagnostic)) {
            std::unordered_map<std::string, uintptr_t> found;
            dynamic_symbols(*image, read, found, diagnostic);
        }
        file_symbols(data, diagnostic);
    }
}
} // namespace
int main() {
    using namespace dumper::elf;
    exercise<Header32, Program32, Dynamic32, Symbol32, Section32, uint32_t>();
    exercise<Header64, Program64, Dynamic64, Symbol64, Section64, uint64_t>();
    std::cout << checks << " ELF checks and 2000 mutation cases passed\n";
}

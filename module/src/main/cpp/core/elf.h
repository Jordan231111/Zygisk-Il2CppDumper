#pragma once
#include "binary.h"
#include "runtime_layout.h"
#include <string>
#include <unordered_map>
#include <vector>
namespace dumper {
namespace elf {
struct Header32 {
    uint8_t ident[16];
    uint16_t type, machine;
    uint32_t version, entry, phoff, shoff, flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};
struct Header64 {
    uint8_t ident[16];
    uint16_t type, machine;
    uint32_t version;
    uint64_t entry, phoff, shoff;
    uint32_t flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};
struct Program32 {
    uint32_t type, offset, vaddr, paddr, filesz, memsz, flags, align;
};
struct Program64 {
    uint32_t type, flags;
    uint64_t offset, vaddr, paddr, filesz, memsz, align;
};
struct Section32 {
    uint32_t name, type, flags, addr, offset, size, link, info, addralign, entsize;
};
struct Section64 {
    uint32_t name, type;
    uint64_t flags, addr, offset, size;
    uint32_t link, info;
    uint64_t addralign, entsize;
};
struct Symbol32 {
    uint32_t name, value, size;
    uint8_t info, other;
    uint16_t section;
};
struct Symbol64 {
    uint32_t name;
    uint8_t info, other;
    uint16_t section;
    uint64_t value, size;
};
struct Dynamic32 {
    int32_t tag;
    uint32_t value;
};
struct Dynamic64 {
    int64_t tag;
    uint64_t value;
};
static_assert(sizeof(Header32) == 52 && sizeof(Header64) == 64);
static_assert(sizeof(Program32) == 32 && sizeof(Program64) == 56);
static_assert(sizeof(Section32) == 40 && sizeof(Section64) == 64);
static_assert(sizeof(Symbol32) == 16 && sizeof(Symbol64) == 24);
constexpr uint32_t kLoad = 1, kDynamic = 2, kExecute = 1, kRead = 4;
struct Segment {
    uint32_t type, flags;
    uint64_t offset, vaddr, filesz, memsz;
};
struct Image {
    bool is64{};
    uint16_t machine{};
    uintptr_t load_bias{}, program_headers{};
    size_t program_header_count{};
    std::vector<Segment> segments;
    bool contains(uintptr_t address, size_t size, uint32_t flags) const;
};
// Kernel/file-reader bounds remain authoritative. No parser dereferences an
// address from the input. ELF virtual addresses and file offsets stay separate.
std::optional<Image> image_from_memory(uintptr_t header, const ReadMemory& read,
                                       std::string& error);
bool dynamic_symbols(const Image& image, const ReadMemory& read,
                     std::unordered_map<std::string, uintptr_t>& result, std::string& error);
std::unordered_map<std::string, uint64_t> file_symbols(std::span<const std::byte> bytes,
                                                       std::string& error);
} // namespace elf
} // namespace dumper

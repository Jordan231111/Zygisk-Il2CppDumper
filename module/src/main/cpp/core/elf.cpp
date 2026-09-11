#include "elf.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
namespace dumper::elf {
namespace {
constexpr size_t kMaxHeaders = 4096;
constexpr size_t kMaxSymbols = 1024 * 1024;
constexpr size_t kMaxStrings = 64 * 1024 * 1024;
bool fail(std::string& error, const char* message) {
    error = message;
    return false;
}
std::optional<uintptr_t> address_add(uintptr_t address, uint64_t offset) {
    if (offset > std::numeric_limits<uintptr_t>::max())
        return {};
    return checked_add(address, static_cast<uintptr_t>(offset));
}
template <class T> bool read_at(const ReadMemory& read, uintptr_t address, T& value) {
    return read(address, &value, sizeof(value));
}
template <class H, class P>
std::optional<Image> parse_header(uintptr_t address, const ReadMemory& read, std::string& error) {
    H header{};
    if (!read_at(read, address, header) || header.ehsize != sizeof(H) ||
        header.phentsize != sizeof(P) || header.phnum == 0 || header.phnum > kMaxHeaders ||
        (header.type != 2 && header.type != 3) || header.version != 1) {
        fail(error, "invalid ELF header");
        return {};
    }
    Image result;
    result.is64 = sizeof(H) == sizeof(Header64);
    result.machine = header.machine;
    const auto programs = address_add(address, header.phoff);
    if (!programs) {
        fail(error, "program header offset overflow");
        return {};
    }
    result.program_headers = *programs;
    result.program_header_count = header.phnum;
    std::optional<uintptr_t> bias;
    for (size_t i = 0; i < header.phnum; ++i) {
        P program{};
        const auto at = address_add(*programs, i * sizeof(P));
        if (!at || !read_at(read, *at, program)) {
            fail(error, "truncated program headers");
            return {};
        }
        if (program.type == kLoad && program.offset == 0 && program.filesz >= sizeof(H)) {
            if (program.vaddr > address) {
                fail(error, "invalid load bias");
                return {};
            }
            const auto candidate = address - static_cast<uintptr_t>(program.vaddr);
            if (bias && *bias != candidate) {
                fail(error, "ambiguous load bias");
                return {};
            }
            bias = candidate;
        }
        if (program.type == kLoad && program.filesz > program.memsz) {
            fail(error, "ELF file size exceeds memory size");
            return {};
        }
        if (program.type == kLoad && program.align > 1 &&
            ((program.align & (program.align - 1)) != 0 ||
             program.vaddr % program.align != program.offset % program.align)) {
            fail(error, "invalid segment alignment");
            return {};
        }
        result.segments.push_back({program.type, program.flags, program.offset, program.vaddr,
                                   program.filesz, program.memsz});
    }
    if (!bias) {
        fail(error, "ELF header is outside load segments");
        return {};
    }
    result.load_bias = *bias;
    for (const auto& segment : result.segments) {
        const auto begin = address_add(*bias, segment.vaddr);
        if (!begin || !address_add(*begin, segment.memsz)) {
            fail(error, "segment address overflow");
            return {};
        }
    }
    return result;
}
std::optional<uintptr_t> dynamic_pointer(const Image& image, uint64_t value, size_t length) {
    const auto relative = address_add(image.load_bias, value);
    if (relative && image.contains(*relative, length, kRead))
        return relative;
    if (value <= std::numeric_limits<uintptr_t>::max() &&
        image.contains(static_cast<uintptr_t>(value), length, kRead))
        return static_cast<uintptr_t>(value);
    return {};
}
template <class D, class S, class Word>
bool parse_dynamic(const Image& image, const ReadMemory& read,
                   std::unordered_map<std::string, uintptr_t>& result, std::string& error) {
    const Segment* dynamic = nullptr;
    for (const auto& segment : image.segments)
        if (segment.type == kDynamic) {
            if (dynamic)
                return fail(error, "multiple dynamic segments");
            dynamic = &segment;
        }
    if (!dynamic || dynamic->memsz < sizeof(D) || dynamic->memsz > 1024 * 1024)
        return fail(error, "missing/oversized dynamic segment");
    const auto start = address_add(image.load_bias, dynamic->vaddr);
    if (!start || !image.contains(*start, static_cast<size_t>(dynamic->memsz), kRead))
        return fail(error, "dynamic segment outside readable loads");
    uint64_t symtab{}, strtab{}, strsz{}, syment{}, hash{}, gnu_hash{};
    bool terminated = false;
    for (size_t offset = 0; offset <= dynamic->memsz - sizeof(D); offset += sizeof(D)) {
        D entry{};
        const auto at = address_add(*start, offset);
        if (!at || !read_at(read, *at, entry))
            return fail(error, "unreadable dynamic entry");
        if (entry.tag == 0) {
            terminated = true;
            break;
        }
        switch (entry.tag) {
        case 4:
            hash = entry.value;
            break;
        case 5:
            strtab = entry.value;
            break;
        case 6:
            symtab = entry.value;
            break;
        case 10:
            strsz = entry.value;
            break;
        case 11:
            syment = entry.value;
            break;
        case 0x6ffffef5:
            gnu_hash = entry.value;
            break;
        default:
            break;
        }
    }
    if (!terminated || syment != sizeof(S) || strsz == 0 || strsz > kMaxStrings)
        return fail(error, "invalid dynamic table/string size");
    const auto strings_address = dynamic_pointer(image, strtab, static_cast<size_t>(strsz));
    const auto symbols_address = dynamic_pointer(image, symtab, sizeof(S));
    if (!strings_address || !symbols_address)
        return fail(error, "symbol/string table outside readable loads");
    size_t count = 0;
    if (hash) {
        const auto header = dynamic_pointer(image, hash, 2 * sizeof(uint32_t));
        std::array<uint32_t, 2> words{};
        if (!header || !read(*header, words.data(), sizeof(words)) || words[0] == 0 ||
            words[0] > kMaxSymbols || words[1] > kMaxSymbols)
            return fail(error, "invalid SYSV hash header");
        const size_t length = (size_t{2} + words[0] + words[1]) * sizeof(uint32_t);
        if (!image.contains(*header, length, kRead))
            return fail(error, "SYSV hash exceeds readable loads");
        count = words[1];
    } else if (gnu_hash) {
        const auto header = dynamic_pointer(image, gnu_hash, 4 * sizeof(uint32_t));
        std::array<uint32_t, 4> words{};
        if (!header || !read(*header, words.data(), sizeof(words)) || words[0] == 0 ||
            words[0] > kMaxSymbols || words[1] > kMaxSymbols || words[2] == 0 ||
            words[2] > kMaxSymbols || (words[2] & (words[2] - 1)) != 0 || words[3] >= 32)
            return fail(error, "invalid GNU hash header");
        const auto buckets_address =
            address_add(*header, sizeof(words) + uint64_t(words[2]) * sizeof(Word));
        if (!buckets_address ||
            !image.contains(*buckets_address, size_t(words[0]) * sizeof(uint32_t), kRead))
            return fail(error, "GNU buckets exceed readable loads");
        std::vector<uint32_t> buckets(words[0]);
        if (!read(*buckets_address, buckets.data(), buckets.size() * sizeof(uint32_t)))
            return fail(error, "unreadable GNU buckets");
        const auto maximum = *std::max_element(buckets.begin(), buckets.end());
        if (maximum >= kMaxSymbols || (maximum != 0 && maximum < words[1]))
            return fail(error, "invalid GNU bucket index");
        count = words[1];
        if (maximum != 0) {
            const auto chains = address_add(*buckets_address, buckets.size() * sizeof(uint32_t));
            if (!chains)
                return fail(error, "GNU chain address overflow");
            count = maximum;
            bool end = false;
            size_t steps = 0;
            while (count < kMaxSymbols && steps++ < 4096) {
                const auto chain_address =
                    address_add(*chains, (count - words[1]) * sizeof(uint32_t));
                uint32_t chain{};
                if (!chain_address || !image.contains(*chain_address, sizeof(chain), kRead) ||
                    !read_at(read, *chain_address, chain))
                    return fail(error, "GNU chain exceeds readable loads");
                ++count;
                if ((chain & 1U) != 0) {
                    end = true;
                    break;
                }
            }
            if (!end)
                return fail(error, "unterminated GNU chain");
        }
    } else
        return fail(error, "missing dynamic symbol count (no supported hash)");
    if (count == 0 || count > kMaxSymbols ||
        !image.contains(*symbols_address, count * sizeof(S), kRead))
        return fail(error, "symbol count outside readable loads");
    std::vector<S> symbols(count);
    std::vector<char> strings(static_cast<size_t>(strsz));
    if (!read(*symbols_address, symbols.data(), symbols.size() * sizeof(S)) ||
        !read(*strings_address, strings.data(), strings.size()))
        return fail(error, "unreadable symbol/string tables");
    for (const auto& symbol : symbols) {
        const auto type = symbol.info & 15U;
        if (symbol.section == 0 || symbol.section >= 0xff00 || (type != 0 && type != 2))
            continue;
        if (symbol.name >= strings.size())
            return fail(error, "symbol name out of bounds");
        const auto* name = strings.data() + symbol.name;
        const auto* end = static_cast<const char*>(
            std::memchr(name, 0, std::min(size_t{4096}, strings.size() - symbol.name)));
        if (!end)
            return fail(error, "unterminated symbol name");
        const auto address = address_add(image.load_bias, symbol.value);
        if (!address ||
            !image.contains(*address & (image.machine == 40 ? ~uintptr_t{1} : ~uintptr_t{0}), 1,
                            kExecute))
            continue;
        const std::string_view symbol_name(name, static_cast<size_t>(end - name));
        if (symbol_name.starts_with("il2cpp_"))
            result.emplace(symbol_name, *address);
    }
    return true;
}
template <class H, class Sh, class S>
std::unordered_map<std::string, uint64_t> parse_file(std::span<const std::byte> bytes,
                                                     std::string& error) {
    std::unordered_map<std::string, uint64_t> result;
    const BinaryView view(bytes);
    const auto header = view.at<H>(0);
    if (!header || header->ehsize != sizeof(H) || header->version != 1 ||
        (header->type != 2 && header->type != 3) || header->shnum == 0 ||
        header->shnum > kMaxHeaders || header->shentsize != sizeof(Sh) ||
        header->shoff > bytes.size() ||
        !bounded(bytes.size(), static_cast<size_t>(header->shoff),
                 size_t(header->shnum) * sizeof(Sh))) {
        fail(error, "missing/invalid section table");
        return {};
    }
    for (size_t i = 0; i < header->shnum; ++i) {
        const auto section = view.at<Sh>(static_cast<size_t>(header->shoff) + i * sizeof(Sh));
        if (!section || section->type != 2)
            continue;
        if (section->entsize != sizeof(S) || section->size % sizeof(S) != 0 ||
            section->size / sizeof(S) > kMaxSymbols || section->link >= header->shnum ||
            section->offset > bytes.size() || section->size > bytes.size() - section->offset) {
            fail(error, "invalid symbol section");
            return {};
        }
        const auto strings =
            view.at<Sh>(static_cast<size_t>(header->shoff) + size_t(section->link) * sizeof(Sh));
        if (!strings || strings->type != 3 || strings->size > kMaxStrings ||
            strings->offset > bytes.size() || strings->size > bytes.size() - strings->offset) {
            fail(error, "invalid linked string table");
            return {};
        }
        for (uint64_t offset = 0; offset < section->size; offset += sizeof(S)) {
            const auto symbol = view.at<S>(static_cast<size_t>(section->offset + offset));
            if (!symbol || symbol->section == 0 || symbol->section >= 0xff00 ||
                ((symbol->info & 15U) != 2 && (symbol->info & 15U) != 0))
                continue;
            if (symbol->name >= strings->size) {
                fail(error, "file symbol name out of bounds");
                return {};
            }
            const auto* name =
                reinterpret_cast<const char*>(bytes.data() + strings->offset + symbol->name);
            const auto* end = static_cast<const char*>(std::memchr(
                name, 0,
                std::min(size_t{4096}, static_cast<size_t>(strings->size - symbol->name))));
            if (!end) {
                fail(error, "unterminated file symbol name");
                return {};
            }
            result.emplace(std::string(name, end), symbol->value);
        }
    }
    return result;
}
} // namespace

bool Image::contains(uintptr_t address, size_t size, uint32_t flags) const {
    for (const auto& segment : segments) {
        if (segment.type != kLoad || (segment.flags & flags) != flags)
            continue;
        const auto begin = address_add(load_bias, segment.vaddr);
        if (!begin || segment.memsz > std::numeric_limits<uintptr_t>::max() || address < *begin)
            continue;
        const auto offset = address - *begin;
        if (offset <= segment.memsz && size <= segment.memsz - offset)
            return true;
    }
    return false;
}
std::optional<Image> image_from_memory(uintptr_t header, const ReadMemory& read,
                                       std::string& error) {
    error.clear();
    std::array<uint8_t, 16> ident{};
    if (!read(header, ident.data(), ident.size()) || std::memcmp(ident.data(), "\177ELF", 4) != 0 ||
        ident[5] != 1 || ident[6] != 1) {
        fail(error, "invalid ELF identification");
        return {};
    }
    if (ident[4] == 1)
        return parse_header<Header32, Program32>(header, read, error);
    if (ident[4] == 2)
        return parse_header<Header64, Program64>(header, read, error);
    fail(error, "unsupported ELF class");
    return {};
}
bool dynamic_symbols(const Image& image, const ReadMemory& read,
                     std::unordered_map<std::string, uintptr_t>& result, std::string& error) {
    error.clear();
    result.clear();
    const bool valid =
        image.is64 ? parse_dynamic<Dynamic64, Symbol64, uint64_t>(image, read, result, error)
                   : parse_dynamic<Dynamic32, Symbol32, uint32_t>(image, read, result, error);
    if (!valid)
        result.clear();
    return valid;
}
std::unordered_map<std::string, uint64_t> file_symbols(std::span<const std::byte> bytes,
                                                       std::string& error) {
    error.clear();
    if (bytes.size() < 16 || std::memcmp(bytes.data(), "\177ELF", 4) != 0 ||
        bytes[5] != std::byte{1} || bytes[6] != std::byte{1}) {
        fail(error, "invalid file ELF identification");
        return {};
    }
    if (bytes[4] == std::byte{1})
        return parse_file<Header32, Section32, Symbol32>(bytes, error);
    if (bytes[4] == std::byte{2})
        return parse_file<Header64, Section64, Symbol64>(bytes, error);
    fail(error, "unsupported file ELF class");
    return {};
}
} // namespace dumper::elf

#include "maps.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <limits>
#include <unistd.h>
#if defined(__linux__)
#include <sys/uio.h>
#endif

namespace dumper {
namespace {
std::string_view token(std::string_view& rest) {
    const auto first = rest.find_first_not_of(" \t");
    if (first == std::string_view::npos) {
        rest = {};
        return {};
    }
    rest.remove_prefix(first);
    const auto end = rest.find_first_of(" \t");
    const auto result = rest.substr(0, end);
    rest.remove_prefix(result.size());
    return result;
}
template <class T> bool number(std::string_view text, T& value, int base) {
    if (text.empty())
        return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, base);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}
} // namespace

std::optional<Mapping> parse_mapping(std::string_view line) {
    Mapping result;
    const auto addresses = token(line);
    const auto dash = addresses.find('-');
    if (dash == std::string_view::npos || !number(addresses.substr(0, dash), result.begin, 16) ||
        !number(addresses.substr(dash + 1), result.end, 16) || result.begin >= result.end)
        return {};
    const auto permissions = token(line);
    if (permissions.size() != 4 || (permissions[0] != 'r' && permissions[0] != '-') ||
        (permissions[1] != 'w' && permissions[1] != '-') ||
        (permissions[2] != 'x' && permissions[2] != '-') ||
        (permissions[3] != 'p' && permissions[3] != 's'))
        return {};
    result.readable = permissions[0] == 'r';
    result.writable = permissions[1] == 'w';
    result.executable = permissions[2] == 'x';
    if (!number(token(line), result.file_offset, 16))
        return {};
    const auto device = token(line);
    const auto colon = device.find(':');
    uint32_t major{}, minor{};
    uint64_t inode{};
    if (colon == std::string_view::npos || !number(device.substr(0, colon), major, 16) ||
        !number(device.substr(colon + 1), minor, 16) || !number(token(line), inode, 10))
        return {};
    const auto first = line.find_first_not_of(" \t");
    if (first != std::string_view::npos) {
        line.remove_prefix(first);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
            line.remove_suffix(1);
        result.path = line;
    }
    return result;
}

bool range_contains(uintptr_t begin, uintptr_t end, uintptr_t address, size_t size) {
    return begin <= end && address >= begin && address <= end && size <= end - address;
}

uintptr_t untag_address(uintptr_t address) {
#if defined(__aarch64__) && defined(__ANDROID__)
    // Android heap pointers may carry a top-byte memory tag (TBI/MTE).
    return address & UINT64_C(0x00ffffffffffffff);
#else
    return address;
#endif
}

Maps Maps::read_self() {
    std::ifstream input("/proc/self/maps");
    return parse(std::string(std::istreambuf_iterator<char>(input), {}));
}

Maps Maps::parse(std::string_view text) {
    Maps maps;
    while (!text.empty()) {
        auto end = text.find('\n');
        if (auto entry = parse_mapping(text.substr(0, end)))
            maps.entries_.push_back(std::move(*entry));
        if (end == std::string_view::npos)
            break;
        text.remove_prefix(end + 1);
    }
    std::sort(maps.entries_.begin(), maps.entries_.end(),
              [](const auto& a, const auto& b) { return a.begin < b.begin; });
    return maps;
}

const Mapping* Maps::find(uintptr_t address, size_t size) const {
    address = untag_address(address);
    const auto it =
        std::upper_bound(entries_.begin(), entries_.end(), address,
                         [](uintptr_t value, const Mapping& entry) { return value < entry.begin; });
    if (it == entries_.begin())
        return nullptr;
    const auto& entry = *std::prev(it);
    return range_contains(entry.begin, entry.end, address, size) ? &entry : nullptr;
}
bool Maps::readable(uintptr_t address, size_t size) const {
    address = untag_address(address);
    while (size != 0) {
        const auto* entry = find(address);
        if (!entry || !entry->readable)
            return false;
        const auto chunk = std::min(size, static_cast<size_t>(entry->end - address));
        size -= chunk;
        if (size == 0)
            return true;
        if (address > std::numeric_limits<uintptr_t>::max() - chunk)
            return false;
        address += chunk;
    }
    return true;
}
bool Maps::executable(uintptr_t address) const {
#if defined(__arm__)
    address &= ~uintptr_t{1}; // Thumb function pointers carry an instruction-set bit.
#endif
    const auto* entry = find(address);
    return entry && entry->executable;
}

Memory::Memory() : fd_(open("/proc/self/mem", O_RDONLY | O_CLOEXEC)) {}
Memory::~Memory() {
    if (fd_ >= 0)
        close(fd_);
}
bool Memory::read(uintptr_t address, void* destination, size_t size) const {
    address = untag_address(address);
    if (size == 0)
        return true;
    if (!destination || address == 0 || size > std::numeric_limits<uintptr_t>::max() - address ||
        size > static_cast<size_t>(std::numeric_limits<ssize_t>::max()))
        return false;
#if defined(__linux__)
    iovec local{destination, size};
    iovec remote{reinterpret_cast<void*>(address), size};
    ssize_t result;
    do {
        result = process_vm_readv(getpid(), &local, 1, &remote, 1, 0);
    } while (result < 0 && errno == EINTR);
    if (result == static_cast<ssize_t>(size))
        return true;
#endif
    if (fd_ < 0 || address > static_cast<uintptr_t>(std::numeric_limits<off_t>::max()))
        return false;
    ssize_t count;
    do {
        count = pread(fd_, destination, size, static_cast<off_t>(address));
    } while (count < 0 && errno == EINTR);
    return count == static_cast<ssize_t>(size);
}
std::optional<std::string> Memory::string(uintptr_t address, Maps& cache, size_t limit) const {
    address = untag_address(address);
    std::string result;
    while (result.size() < limit) {
        const Mapping* mapping = cache.find(address);
        // Allocators reserve PROT_NONE arenas and commit them later. A cached
        // mapping can still exist while its permissions have become readable.
        if (!mapping || !mapping->readable) {
            cache = Maps::read_self();
            mapping = cache.find(address);
        }
        if (!mapping || !mapping->readable)
            return {};
        std::array<char, 128> buffer{};
        const size_t count =
            std::min({buffer.size(), size_t(mapping->end - address), limit - result.size()});
        if (!read(address, buffer.data(), count))
            return {};
        for (size_t i = 0; i < count; ++i) {
            if (buffer[i] == 0)
                return result;
            result += buffer[i];
        }
        address += count;
    }
    return {};
}
bool Memory::read_pointers(std::span<const uintptr_t> addresses,
                           std::span<uintptr_t> values) const {
    if (values.size() != addresses.size())
        return false;
    std::fill(values.begin(), values.end(), 0);
    bool complete = true;
    constexpr size_t capacity = 64;
#if defined(__linux__)
    const long supported = sysconf(_SC_IOV_MAX);
    const size_t batch_limit =
        supported > 0 ? std::min(capacity, static_cast<size_t>(supported)) : 1;
#else
    const size_t batch_limit = capacity;
#endif
    for (size_t start = 0; start < addresses.size(); start += batch_limit) {
        const size_t count = std::min(batch_limit, addresses.size() - start);
#if defined(__linux__)
        std::array<iovec, capacity> local{}, remote{};
        bool valid = true;
        for (size_t i = 0; i < count; ++i) {
            const auto address = untag_address(addresses[start + i]);
            valid &= address != 0 &&
                     address <= std::numeric_limits<uintptr_t>::max() - sizeof(uintptr_t);
            local[i] = {&values[start + i], sizeof(uintptr_t)};
            remote[i] = {reinterpret_cast<void*>(address), sizeof(uintptr_t)};
        }
        ssize_t read_bytes = -1;
        if (valid) {
            do {
                read_bytes =
                    process_vm_readv(getpid(), local.data(), count, remote.data(), count, 0);
            } while (read_bytes < 0 && errno == EINTR);
        }
        if (read_bytes == static_cast<ssize_t>(count * sizeof(uintptr_t)))
            continue;
#endif
        // A failed/partial batch never hides an unreadable entry. Preserve good
        // entries and use the same checked fallback as individual reads.
        for (size_t i = 0; i < count; ++i) {
            const auto value = read<uintptr_t>(addresses[start + i]);
            values[start + i] = value.value_or(0);
            complete &= value.has_value();
        }
    }
    return complete;
}
} // namespace dumper

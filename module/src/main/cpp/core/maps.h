#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace dumper {
struct Mapping {
    uintptr_t begin{};
    uintptr_t end{};
    uint64_t file_offset{};
    bool readable{};
    bool writable{};
    bool executable{};
    std::string path;
};

std::optional<Mapping> parse_mapping(std::string_view line);
bool range_contains(uintptr_t begin, uintptr_t end, uintptr_t address, size_t size);
uintptr_t untag_address(uintptr_t address);

class Maps {
  public:
    static Maps read_self();
    static Maps parse(std::string_view text);
    const Mapping* find(uintptr_t address, size_t size = 1) const;
    bool readable(uintptr_t address, size_t size) const;
    bool executable(uintptr_t address) const;
    const std::vector<Mapping>& entries() const { return entries_; }

  private:
    std::vector<Mapping> entries_;
};

// Kernel-assisted reads avoid installing process-wide signal handlers and fail
// on unmapped memory, including mappings removed after the maps snapshot.
class Memory {
  public:
    Memory();
    ~Memory();
    Memory(const Memory&) = delete;
    Memory& operator=(const Memory&) = delete;
    bool read(uintptr_t address, void* destination, size_t size) const;
    bool read_pointers(std::span<const uintptr_t> addresses, std::span<uintptr_t> values) const;
    std::optional<std::string> string(uintptr_t address, Maps& cache, size_t limit = 65536) const;
    template <class T> std::optional<T> read(uintptr_t address) const {
        static_assert(std::is_trivially_copyable_v<T>);
        T value{};
        if (!read(address, &value, sizeof(value)))
            return std::nullopt;
        return value;
    }

  private:
    int fd_{-1};
};
} // namespace dumper

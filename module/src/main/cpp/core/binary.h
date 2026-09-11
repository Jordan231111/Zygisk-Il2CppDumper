#pragma once
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

namespace dumper {
constexpr bool bounded(size_t total, size_t offset, size_t length) {
    return offset <= total && length <= total - offset;
}
constexpr std::optional<uintptr_t> checked_add(uintptr_t a, uintptr_t b) {
    if (b > std::numeric_limits<uintptr_t>::max() - a)
        return {};
    return a + b;
}
class BinaryView {
  public:
    explicit BinaryView(std::span<const std::byte> bytes) : bytes_(bytes) {}
    template <class T> std::optional<T> at(size_t offset) const {
        static_assert(std::is_trivially_copyable_v<T>);
        if (!bounded(bytes_.size(), offset, sizeof(T)))
            return {};
        T value{};
        std::memcpy(&value, bytes_.data() + offset, sizeof(T));
        return value;
    }
    size_t size() const { return bytes_.size(); }

  private:
    std::span<const std::byte> bytes_;
};
struct MetadataHeader {
    uint32_t magic;
    uint32_t version;
};
inline std::optional<MetadataHeader> metadata_header(const BinaryView& data) {
    static_assert(std::endian::native == std::endian::little);
    const auto header = data.at<MetadataHeader>(0);
    // Only identify the common prefix. Later versions have different tables and
    // variable-width indices; accepting this prefix is NOT full layout validation.
    if (!header || header->magic != 0xfab11bafU || header->version == 0)
        return {};
    return header;
}
} // namespace dumper

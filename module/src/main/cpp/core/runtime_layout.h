#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
namespace dumper {
using ReadMemory = std::function<bool(uintptr_t, void*, size_t)>;
// Recognize the bounded AArch64 lazy-global getter emitted by Clang:
// ADRP/ADR + optional ADD + LDR x0 + CBNZ x0. This discovers a slot from
// instructions, never a fixed Unity offset. Unsupported code returns no match.
struct PointerGetter {
    uintptr_t address;
    bool indirect;
    bool lazy;
};
std::optional<PointerGetter> aarch64_pointer_getter(uintptr_t entry, const ReadMemory& read);
std::optional<uintptr_t> aarch64_lazy_global(uintptr_t entry, const ReadMemory& read);
} // namespace dumper

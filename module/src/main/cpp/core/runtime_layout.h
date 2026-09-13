#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
namespace dumper {
using ReadMemory = std::function<bool(uintptr_t, void*, size_t)>;
// Recognize the bounded AArch64 lazy-global getter emitted by Clang:
// ADRP/ADR + optional ADD, optional GOT load, then LDR x0 + CBNZ/RET.
// This discovers slots from instructions, never fixed Unity offsets.
// Unsupported code returns no match; at most two data loads are described.
struct PointerChain {
    uintptr_t global;
    size_t offset;
};
std::optional<PointerChain> aarch64_vm_thread_chain(uintptr_t entry, const ReadMemory& read);
struct PointerGetter {
    uintptr_t address;
    bool indirect;
    bool lazy;
    // If present, load a base pointer from address and add this byte offset
    // before applying indirect. This covers a GOT entry followed by a global.
    std::optional<size_t> base_offset{};
};
std::optional<PointerGetter> aarch64_pointer_getter(uintptr_t entry, const ReadMemory& read);
std::optional<uintptr_t> aarch64_lazy_global(uintptr_t entry, const ReadMemory& read);
} // namespace dumper

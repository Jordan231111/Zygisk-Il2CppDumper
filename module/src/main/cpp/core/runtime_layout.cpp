#include "runtime_layout.h"
#include "binary.h"
#include <array>
#include <limits>
namespace dumper {
namespace {
int64_t sign_extend(uint32_t value, unsigned bits) {
    const uint32_t sign = uint32_t{1} << (bits - 1);
    return static_cast<int64_t>(value ^ sign) - sign;
}
std::optional<uintptr_t> add_signed(uintptr_t base, int64_t displacement) {
    if (displacement < 0) {
        const auto magnitude = static_cast<uint64_t>(-displacement);
        if (magnitude > base)
            return {};
        return base - static_cast<uintptr_t>(magnitude);
    }
    return checked_add(base, static_cast<uintptr_t>(displacement));
}
} // namespace
std::optional<PointerGetter> aarch64_pointer_getter(uintptr_t entry, const ReadMemory& read) {
    if ((entry & 3U) != 0)
        return {};
    struct AddressValue {
        uintptr_t address;
        bool loaded{};
        size_t offset{};
    };
    std::array<std::optional<AddressValue>, 31> registers{};
    auto pc = entry;
    unsigned branches = 0;
    for (unsigned i = 0; i < 32; ++i) {
        uint32_t instruction{};
        if (!read(pc, &instruction, sizeof(instruction)))
            return {};
        if ((instruction & 0xfc000000U) == 0x14000000U) {
            if (++branches > 4)
                return {};
            const auto target = add_signed(pc, sign_extend(instruction & 0x03ffffffU, 26) * 4);
            if (!target)
                return {};
            pc = *target;
            registers.fill({});
            continue;
        }
        const auto destination = instruction & 31U;
        if ((instruction & 0x9f000000U) == 0x90000000U ||
            (instruction & 0x9f000000U) == 0x10000000U) {
            if (destination == 31)
                return {};
            const auto immediate =
                ((instruction >> 29) & 3U) | (((instruction >> 5) & 0x7ffffU) << 2);
            const bool page = (instruction & 0x80000000U) != 0;
            const auto address = add_signed(page ? pc & ~uintptr_t{4095} : pc,
                                            sign_extend(immediate, 21) * (page ? 4096 : 1));
            if (!address)
                return {};
            registers[destination] = AddressValue{*address};
            // ADRP's 4 KB unit is an ISA encoding, independent of OS page size.
        } else if ((instruction & 0xff800000U) == 0x91000000U) {
            const auto source = (instruction >> 5) & 31U;
            if (destination != 31 && source != 31 && registers[source]) {
                const auto immediate = ((instruction >> 10) & 4095U)
                                       << (((instruction >> 22) & 1U) ? 12 : 0);
                auto value = *registers[source];
                const auto updated =
                    checked_add(value.loaded ? value.offset : value.address, immediate);
                if (!updated)
                    return {};
                if (value.loaded)
                    value.offset = *updated;
                else
                    value.address = *updated;
                registers[destination] = value;
            } else if (destination != 31)
                registers[destination].reset();
        } else if ((instruction & 0xffc00000U) == 0xf9400000U) {
            const auto source = (instruction >> 5) & 31U;
            if (destination == 31 || source == 31 || !registers[source])
                return {};
            const auto value = *registers[source];
            const auto offset = ((instruction >> 10) & 4095U) * 8;
            const auto next = checked_add(pc, 4);
            uint32_t branch{};
            const bool returns_value =
                destination == 0 && next && read(*next, &branch, sizeof(branch)) &&
                ((branch & 0xff00001fU) == 0xb5000000U || branch == 0xd65f03c0U);
            if (value.loaded) {
                // Two data loads are sufficient for a GOT-indirect global. Do
                // not speculate through deeper chains or arbitrary instructions.
                const auto final_offset = checked_add(value.offset, offset);
                if (returns_value && final_offset)
                    return PointerGetter{value.address, true, branch != 0xd65f03c0U, *final_offset};
                return {};
            }
            const auto slot = checked_add(value.address, offset);
            if (!slot)
                return {};
            if (returns_value)
                return PointerGetter{*slot, true, branch != 0xd65f03c0U};
            registers[destination] = AddressValue{*slot, true, 0};
        } else if (instruction == 0xd65f03c0U && registers[0]) {
            const auto value = *registers[0];
            return PointerGetter{value.address, false, false,
                                 value.loaded ? std::optional<size_t>(value.offset) : std::nullopt};
        } else {
            // Only permit the common stack prologue and architectural landing
            // instructions before a match. Stop at calls, returns or unknown
            // register writes so stale register values cannot create a match.
            const bool landing = instruction == 0xd503245fU || instruction == 0xd503233fU ||
                                 instruction == 0xd503237fU || instruction == 0xd503201fU;
            const bool stack_store = ((instruction & 0x3b4003e0U) == 0x290003e0U) ||
                                     ((instruction & 0xffe003e0U) == 0xf80003e0U);
            const bool frame = instruction == 0x910003fdU;
            if (!landing && !stack_store && !frame)
                return {};
        }
        const auto next = checked_add(pc, 4);
        if (!next)
            return {};
        pc = *next;
    }
    return {};
}
std::optional<uintptr_t> aarch64_lazy_global(uintptr_t entry, const ReadMemory& read) {
    const auto getter = aarch64_pointer_getter(entry, read);
    if (getter && getter->lazy && getter->indirect && !getter->base_offset)
        return getter->address;
    return {};
}
} // namespace dumper

namespace dumper {
std::optional<PointerChain> aarch64_vm_thread_chain(uintptr_t entry, const ReadMemory& read) {
    if ((entry & 3U) != 0)
        return {};
    // Supported wrapper: tail branch, optional BTI/PAC/stack prologue, then a
    // call to a pure pointer comparison. No recovered function is executed.
    uintptr_t pc = entry;
    unsigned jumps = 0;
    bool followed_call = false;
    for (unsigned step = 0; step < 24; ++step) {
        std::array<uint32_t, 6> code{};
        if (!read(pc, code.data(), sizeof(code)))
            return {};
        const auto instruction = code[0];
        if ((instruction & 0xfc000000U) == 0x14000000U ||
            (!followed_call && (instruction & 0xfc000000U) == 0x94000000U)) {
            if (++jumps > 4)
                return {};
            followed_call |= (instruction & 0xfc000000U) == 0x94000000U;
            const auto target = add_signed(pc, sign_extend(instruction & 0x03ffffffU, 26) * 4);
            if (!target)
                return {};
            pc = *target;
            continue;
        }
        if ((instruction & 0x9f000000U) == 0x90000000U) {
            const uint32_t reg = instruction & 31U;
            const auto immediate =
                ((instruction >> 29) & 3U) | (((instruction >> 5) & 0x7ffffU) << 2);
            const auto page = add_signed(pc & ~uintptr_t{4095}, sign_extend(immediate, 21) * 4096);
            if (!page || reg == 31)
                return {};
            const uint32_t load_mask = 0xf9400000U | (reg << 5) | reg;
            const bool loads =
                (code[1] & 0xffc003ffU) == load_mask && (code[2] & 0xffc003ffU) == load_mask;
            const uint32_t compare = 0xeb00001fU | (reg << 5); // CMP Xreg, X0
            if (loads && code[3] == compare && code[4] == 0x1a9f17e0U && code[5] == 0xd65f03c0U) {
                const auto global = checked_add(*page, ((code[1] >> 10) & 4095U) * 8);
                if (global)
                    return PointerChain{*global, size_t((code[2] >> 10) & 4095U) * 8};
            }
            return {};
        }
        const bool landing = instruction == 0xd503245fU || instruction == 0xd503233fU ||
                             instruction == 0xd503237fU || instruction == 0xd503201fU;
        const bool stack_store = (instruction & 0x3b4003e0U) == 0x290003e0U ||
                                 (instruction & 0xffe003e0U) == 0xf80003e0U;
        if (!landing && !stack_store)
            return {};
        const auto next = checked_add(pc, 4);
        if (!next)
            return {};
        pc = *next;
    }
    return {};
}
} // namespace dumper

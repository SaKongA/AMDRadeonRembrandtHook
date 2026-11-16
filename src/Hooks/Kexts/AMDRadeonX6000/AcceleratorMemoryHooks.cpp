#include "Kexts/AMDRadeonX6000/AcceleratorHooks.hpp"

mach_vm_address_t originalUpdateHardwareClocks;

uint64_t wrapUpdateHardwareClocks(uint64_t self, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
    auto result = reinterpret_cast<AcceleratorHookFunction>(originalUpdateHardwareClocks)(self, b, c, d, e, f);
    auto systemClock = reinterpret_cast<uint64_t *>(self + 0x198);
    auto memoryClock = reinterpret_cast<uint64_t *>(self + 0x1a0);
    if (!*systemClock || !*memoryClock) {
        if (!*systemClock) *systemClock = 220000;
        if (!*memoryClock) *memoryClock = 160000;
        result = 1;
    }
    return result;
}

uint64_t wrapScanoutDcc(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) {
    return 0;
}

uint64_t wrapAdjustVramAddress(uint64_t memory, uint64_t address, uint64_t, uint64_t, uint64_t, uint64_t) {
    uint64_t base = *reinterpret_cast<uint64_t *>(memory + 0x50);
    uint64_t physical = *reinterpret_cast<uint64_t *>(memory + 0x58);
    uint64_t size = *reinterpret_cast<uint64_t *>(memory + 0x40) * *reinterpret_cast<uint32_t *>(memory + 0x1b8);
    return address >= base && address < base + size ? address - base + physical : address;
}

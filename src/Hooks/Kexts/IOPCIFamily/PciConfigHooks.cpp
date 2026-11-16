#include "Kexts/IOPCIFamily/PciConfigHooks.hpp"
#include "Diagnostics/KernelLog.hpp"
#include <IOKit/pci/IOPCIDevice.h>

namespace {

using Read8Function = uint8_t (*)(void *, uint64_t);
using Read16Function = uint16_t (*)(void *, uint64_t);
using Read32Function = uint32_t (*)(void *, uint64_t);

mach_vm_address_t originalRead8;
mach_vm_address_t originalRead16;
mach_vm_address_t originalRead32;

uint32_t adjustPciRevision(void *device, uint64_t offset, uint32_t value) {
    if (offset != kIOPCIConfigRevisionID || (value & 0xff) != 0x02) return value;
    auto read16 = reinterpret_cast<Read16Function>(originalRead16);
    if (read16(device, kIOPCIConfigVendorID) != 0x1002 || read16(device, kIOPCIConfigDeviceID) != 0x1681) return value;
    return (value & ~0xffu) | 0xc7;
}

uint8_t wrapReadConfig8(void *device, uint64_t offset) {
    uint8_t value = reinterpret_cast<Read8Function>(originalRead8)(device, offset);
    return static_cast<uint8_t>(adjustPciRevision(device, offset, value));
}

uint16_t wrapReadConfig16(void *device, uint64_t offset) {
    auto read16 = reinterpret_cast<Read16Function>(originalRead16);
    uint16_t value = read16(device, offset);
    if (offset == kIOPCIConfigDeviceID && value == 0x1681 && read16(device, kIOPCIConfigVendorID) == 0x1002)
        value = 0x73ff;
    return static_cast<uint16_t>(adjustPciRevision(device, offset, value));
}

uint32_t wrapReadConfig32(void *device, uint64_t offset) {
    uint32_t value = reinterpret_cast<Read32Function>(originalRead32)(device, offset);
    if (offset == kIOPCIConfigVendorID && value == 0x16811002) value = 0x73ff1002;
    return adjustPciRevision(device, offset, value);
}

}

void installPciConfigHooks(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
    KernelPatcher::RouteRequest requests[] {
        {"__ZN11IOPCIDevice20extendedConfigRead16Ey", wrapReadConfig16, originalRead16},
        {"__ZN11IOPCIDevice20extendedConfigRead32Ey", wrapReadConfig32, originalRead32},
        {"__ZN11IOPCIDevice19extendedConfigRead8Ey", wrapReadConfig8, originalRead8},
    };
    if (patcher.routeMultiple(index, requests, address, size)) KernelLog::logInfo(KernelLog::Category::Hook, "IOPCI", "install", "PCI configuration hooks installed");
    else KernelLog::logError(KernelLog::Category::Hook, "IOPCI", "install", "PCI configuration hooks failed: error=%d", static_cast<int>(patcher.getError()));
    patcher.clearError();
}

#include "Kexts/IOUSBHostFamily/AppleUSBXHCI/XhciInterruptHooks.hpp"
#include "Diagnostics/KernelLog.hpp"
#include <Headers/kern_mach.hpp>

namespace {

struct InterruptTargetPattern {
    uint8_t bytes[16];
    uint8_t length;
    uint8_t targetOffset;
};

constexpr InterruptTargetPattern patterns[] {
    {{0x48, 0x89, 0x01, 0xc7, 0x41, 0x08, 0x08, 0x00, 0x40, 0x00}, 10, 8},
    {{0xc7, 0x44, 0x08, 0x08, 0x00, 0x00, 0x40, 0x00, 0xc7, 0x44, 0x08, 0x0c, 0x21, 0x04, 0x00, 0x00}, 16, 6},
    {{0x44, 0x09, 0xf1, 0x81, 0xc9, 0x00, 0x00, 0x40, 0x00, 0x42, 0x89, 0x4c, 0x2a, 0x08}, 14, 7},
    {{0x48, 0xc7, 0x04, 0x18, 0x00, 0x00, 0x00, 0x00, 0xc7, 0x44, 0x18, 0x08, 0x00, 0x00, 0x40, 0x00}, 16, 14},
    {{0xc7, 0x44, 0x08, 0x08, 0x00, 0x00, 0x40, 0x00, 0xc7, 0x44, 0x08, 0x0c, 0x00, 0x18, 0x00, 0x00}, 16, 6},
    {{0x41, 0xb8, 0x00, 0x00, 0x40, 0x00, 0x44, 0x89, 0x44, 0x3e, 0x08}, 11, 4},
};

}

void patchXhciInterruptTargets(mach_vm_address_t address, size_t size) {
    auto bytes = reinterpret_cast<uint8_t *>(address);
    size_t count = 0;
    if (MachInfo::setKernelWriting(true, KernelPatcher::kernelWriteLock) != KERN_SUCCESS) {
        KernelLog::logError(KernelLog::Category::Hook, "XHCI", "patch", "Unable to enable kernel writing");
        return;
    }
    for (const auto &pattern : patterns) {
        for (size_t i = 0; i + pattern.length <= size; ++i) {
            if (!memcmp(bytes + i, pattern.bytes, pattern.length)) {
                bytes[i + pattern.targetOffset] = 0;
                ++count;
            }
        }
    }
    MachInfo::setKernelWriting(false, KernelPatcher::kernelWriteLock);
    if (count) KernelLog::logInfo(KernelLog::Category::Hook, "XHCI", "patch", "Patched %lu interrupt targets", static_cast<unsigned long>(count));
    else KernelLog::logWarning(KernelLog::Category::Hook, "XHCI", "patch", "No interrupt target patterns found");
}

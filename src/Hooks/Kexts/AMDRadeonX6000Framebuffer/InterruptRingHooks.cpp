#include "Kexts/AMDRadeonX6000Framebuffer/FramebufferHooks.hpp"
#include "Diagnostics/KernelLog.hpp"
#include <Headers/kern_mach.hpp>

namespace {

using GetWritePointer = int (*)(void *, uint8_t *);
GetWritePointer originalGetWritePointer[2];

void flushInterruptEntries(uint8_t *ring, uint32_t first, uint32_t last) {
    auto base = *reinterpret_cast<uint8_t **>(ring);
    uint32_t count = *reinterpret_cast<uint32_t *>(ring + 0x2c);
    uint32_t entrySize = *reinterpret_cast<uint32_t *>(ring + 0x30);
    if (!base || !count || entrySize != 32 || first >= count || last >= count) return;
    for (uint32_t i = first, n = 0; i != last && n < count; i = (i + 1) % count, ++n) {
        auto entry = base + i * entrySize;
        asm volatile("clflush (%0)" :: "r"(entry) : "memory");
        asm volatile("mfence" ::: "memory");
    }
}

template <size_t index>
int wrapGetWritePointer(void *controller, uint8_t *ring) {
    uint32_t previous = *reinterpret_cast<uint32_t *>(ring + 0x44);
    int result = originalGetWritePointer[index](controller, ring);
    if (result) flushInterruptEntries(ring, previous, *reinterpret_cast<uint32_t *>(ring + 0x44));
    return result;
}

}

void installInterruptRingHooks(KernelPatcher &patcher, mach_vm_address_t address, size_t size) {
    constexpr uint8_t pattern[] {0xf6, 0x86, 0xe4, 0x00, 0x00, 0x00, 0x02, 0x75, 0x0d, 0xbe, 0x24, 0x11, 0x00, 0x00,
        0xff, 0x97, 0xb0, 0x43, 0x00, 0x00, 0xeb, 0x06, 0x48, 0x8b, 0x43, 0x48, 0x8b, 0x00};
    constexpr uint8_t prologue[] {0x55, 0x48, 0x89, 0xe5, 0x53, 0x50, 0x48, 0x89, 0xf3};
    GetWritePointer wrappers[] {wrapGetWritePointer<0>, wrapGetWritePointer<1>};
    uint8_t *functions[2] {};
    auto bytes = reinterpret_cast<uint8_t *>(address);
    size_t count = 0;
    if (MachInfo::setKernelWriting(true, KernelPatcher::kernelWriteLock) != KERN_SUCCESS) {
        KernelLog::logError(KernelLog::Category::Hook, "InterruptRing", "patch", "Unable to enable kernel writing");
        return;
    }
    for (size_t i = 0; i + sizeof(pattern) <= size; ++i) {
        if (memcmp(bytes + i, pattern, sizeof(pattern))) continue;
        bytes[i + 7] = bytes[i + 8] = 0x90;
        if (count < 2 && i >= sizeof(prologue)) functions[count] = bytes + i - sizeof(prologue);
        ++count;
    }
    MachInfo::setKernelWriting(false, KernelPatcher::kernelWriteLock);
    KernelLog::logInfo(KernelLog::Category::Hook, "InterruptRing", "patch", "Found %lu matching sequences", static_cast<unsigned long>(count));
    for (size_t i = 0; i < count && i < 2; ++i) {
        if (!functions[i] || memcmp(functions[i], prologue, sizeof(prologue))) {
            KernelLog::logWarning(KernelLog::Category::Hook, "InterruptRing", "install", "Unexpected function prologue: index=%lu", static_cast<unsigned long>(i));
            continue;
        }
        originalGetWritePointer[i] = reinterpret_cast<GetWritePointer>(patcher.routeFunction(
            reinterpret_cast<mach_vm_address_t>(functions[i]), reinterpret_cast<mach_vm_address_t>(wrappers[i]), true));
        if (!originalGetWritePointer[i]) KernelLog::logError(KernelLog::Category::Hook, "InterruptRing", "install", "Hook failed: index=%lu error=%d", static_cast<unsigned long>(i), static_cast<int>(patcher.getError()));
        else KernelLog::logInfo(KernelLog::Category::Hook, "InterruptRing", "install", "Hook installed: index=%lu", static_cast<unsigned long>(i));
        patcher.clearError();
    }
}

#include <Headers/kern_api.hpp>
#include <Headers/kern_mach.hpp>
#include <sys/vnode.h>
#include "Bundles/AMDRadeonX6000MTLDriver/MetalSwizzleHooks.hpp"
#include "Diagnostics/KernelLog.hpp"

namespace {

constexpr uint8_t xorPattern[] {
    0x48, 0x63, 0x04, 0x87, 0x48, 0x01, 0xf8, 0x8b,
    0x76, 0x04, 0x83, 0xe6, 0x07, 0x8b, 0x04, 0xb0,
    0x80, 0xc1, 0x02, 0xd3, 0xe0, 0x89, 0x42, 0x04
};
constexpr uint8_t xorReplacement[] {
    0x31, 0xc0, 0x0f, 0x1f, 0x40, 0x00, 0x66, 0x90
};
constexpr size_t replacementOffset = 13;

mach_vm_address_t originalCsValidateRange;
mach_vm_address_t originalCsValidatePage;

bool isMetalDriverVnode(void *vnode) {
    if (!vnode) return false;
    const char *name = vnode_getname(static_cast<vnode_t>(vnode));
    if (!name) return false;
    bool matches = !strcmp(name, "AMDRadeonX6000MTLDriver");
    vnode_putname(name);
    return matches;
}
void writeXorReplacement(uint8_t *address) {
    if (MachInfo::setKernelWriting(true, KernelPatcher::kernelWriteLock) != KERN_SUCCESS) {
        return;
    }

    memcpy(address, xorReplacement, sizeof(xorReplacement));
    MachInfo::setKernelWriting(false, KernelPatcher::kernelWriteLock);
}

void patchValidatedPage(const void *data, size_t size) {
    auto bytes = static_cast<uint8_t *>(const_cast<void *>(data));
    if (!bytes || size < sizeof(xorPattern)) {
        return;
    }

    for (size_t split = 1; split <= replacementOffset; ++split) {
        if (!memcmp(bytes, xorPattern + split, sizeof(xorPattern) - split)) {
            if (memcmp(bytes + replacementOffset - split, xorReplacement, sizeof(xorReplacement))) {
                writeXorReplacement(bytes + replacementOffset - split);
            }
            break;
        }
    }

    for (size_t offset = 0, end = size - sizeof(xorPattern); offset <= end; ++offset) {
        if (bytes[offset] != 0x48 || memcmp(bytes + offset, xorPattern, sizeof(xorPattern))) {
            continue;
        }
        writeXorReplacement(bytes + offset + replacementOffset);
    }
}

boolean_t wrapCsValidateRange(void *vnode, memory_object_t pager, memory_object_offset_t offset,
                              const void *data, vm_size_t size, unsigned *tainted) {
    boolean_t result = FunctionCast(wrapCsValidateRange, originalCsValidateRange)(
        vnode, pager, offset, data, size, tainted);
    if (result && isMetalDriverVnode(vnode)) {
        patchValidatedPage(data, size);
    }
    return result;
}

void wrapCsValidatePage(void *vnode, memory_object_t pager, memory_object_offset_t offset,
                        const void *data, int *validated, int *tainted, int *nonExecutable) {
    FunctionCast(wrapCsValidatePage, originalCsValidatePage)(
        vnode, pager, offset, data, validated, tainted, nonExecutable);
    if (validated && *validated && tainted && !*tainted && isMetalDriverVnode(vnode)) {
        patchValidatedPage(data, PAGE_SIZE);
    }
}

}

bool installMetalSwizzleHooks(KernelPatcher &patcher) {
    KernelPatcher::RouteRequest requests[] {
        {"_cs_validate_page", wrapCsValidatePage, originalCsValidatePage},
        {"_cs_validate_range", wrapCsValidateRange, originalCsValidateRange},
    };
    bool installed = patcher.routeMultipleLong(KernelPatcher::KernelID, requests, arrsize(requests));
    if (installed) KernelLog::logInfo(KernelLog::Category::Hook, "Metal", "install", "Code validation hooks installed");
    else KernelLog::logError(KernelLog::Category::Hook, "Metal", "install", "Code validation hooks failed: error=%d", static_cast<int>(patcher.getError()));
    patcher.clearError();
    return installed;
}

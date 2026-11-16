#include "Kexts/AMDRadeonX6000HWServices/AMDRadeonX6000HWLibs/HWLibsHooks.hpp"
#include "Diagnostics/KernelLog.hpp"

namespace {

using HookFunction = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
mach_vm_address_t originalSetDeviceCapability;
mach_vm_address_t originalGetUmaPhysicalFramebufferOffset;
mach_vm_address_t originalInitializeTtl;

uint32_t wrapInitializeTtl(void *services, const uint64_t *input) {
    bool hasCallbacks = input && input[2];
    auto result = reinterpret_cast<uint32_t (*)(void *, const uint64_t *)>(originalInitializeTtl)(services, input);
    KernelLog::logInfo(KernelLog::Category::Hook, "TTL", "initialize", "Callbacks present=%u result=0x%08x",
                       static_cast<unsigned int>(hasCallbacks), result);
    return result;
}

uint64_t wrapSetDeviceCapability(uint64_t context, uint64_t device, uint64_t revision,
                                 uint64_t externalRevision, uint64_t e, uint64_t f) {
    return reinterpret_cast<HookFunction>(originalSetDeviceCapability)(context, device, 0, externalRevision, e, f);
}

uint64_t wrapGetUmaPhysicalFramebufferOffset(uint64_t vm, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
    *reinterpret_cast<uint8_t *>(vm + 0xc) |= 0x8;
    return reinterpret_cast<HookFunction>(originalGetUmaPhysicalFramebufferOffset)(vm, b, c, d, e, f);
}

}

void installTtlDeviceHooks(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
    KernelPatcher::RouteRequest requests[] {
        {"_ttlSetDeviceCapabilityEntry", wrapSetDeviceCapability, originalSetDeviceCapability},
        {"_vm_10_1_get_uma_physical_fb_offset", wrapGetUmaPhysicalFramebufferOffset, originalGetUmaPhysicalFramebufferOffset},
        {"__ZN14AmdTtlServices10initializeEP30_TtlLibraryInitializationInput", wrapInitializeTtl, originalInitializeTtl},
    };
    for (auto &request : requests) {
        if (patcher.routeMultiple(index, &request, 1, address, size))
            KernelLog::logInfo(KernelLog::Category::Hook, "HWLibs", "install TTL", "Hook installed: %s", request.symbol);
        else KernelLog::logError(KernelLog::Category::Hook, "HWLibs", "install TTL", "Hook failed: %s error=%d", request.symbol, static_cast<int>(patcher.getError()));
        patcher.clearError();
    }
}

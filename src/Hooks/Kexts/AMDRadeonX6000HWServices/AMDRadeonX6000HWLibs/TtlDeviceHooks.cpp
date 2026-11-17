#include "Kexts/AMDRadeonX6000HWServices/AMDRadeonX6000HWLibs/HWLibsHooks.hpp"
#include "Diagnostics/KernelLog.hpp"
#include "Kexts/IOPCIFamily/PciConfigHooks.hpp"

namespace {

using HookFunction = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
mach_vm_address_t originalSetDeviceCapability;
mach_vm_address_t originalGetUmaPhysicalFramebufferOffset;
mach_vm_address_t originalInitializeTtl;
mach_vm_address_t originalInitializeTtlServices;
mach_vm_address_t originalInitializeTtlRaw;

uint32_t wrapInitializeTtl(void *services, const uint64_t *input) {
    bool hasInput = input != nullptr;
    bool hasField0 = input && input[0];
    bool hasField1 = input && input[1];
    bool hasField2 = input && input[2];
    KernelLog::logDiagnosticInfo(KernelLog::Category::Hook, "TTL", "initialize input", "Enter: services=%u input=%u fields[0..2]=%u,%u,%u",
                                 static_cast<unsigned int>(services != nullptr), static_cast<unsigned int>(hasInput),
                                 static_cast<unsigned int>(hasField0), static_cast<unsigned int>(hasField1), static_cast<unsigned int>(hasField2));
    auto result = reinterpret_cast<uint32_t (*)(void *, const uint64_t *)>(originalInitializeTtl)(services, input);
    KernelLog::logDiagnosticInfo(KernelLog::Category::Hook, "TTL", "initialize input", "Return: result=0x%08x", result);
    return result;
}

uint32_t wrapInitializeTtlServices(void *services) {
    KernelLog::logDiagnosticInfo(KernelLog::Category::Hook, "TTL", "initialize services", "Enter");
    auto result = reinterpret_cast<uint32_t (*)(void *)>(originalInitializeTtlServices)(services);
    KernelLog::logDiagnosticInfo(KernelLog::Category::Hook, "TTL", "initialize services", "Return: result=0x%08x", result);
    return result;
}

uint32_t wrapInitializeTtlRaw(void *parameters, void *context) {
    if (parameters) {
        auto bytes = static_cast<const uint8_t *>(parameters);
        uint32_t deviceId = *reinterpret_cast<const uint32_t *>(bytes + 32);
        uint32_t revision = *reinterpret_cast<const uint32_t *>(bytes + 132);
        KernelLog::logInfo(KernelLog::Category::Hook, "TTL", "check identity", "Device=%04x revision=%04x PCI hooks installed=%u",
            deviceId, revision, static_cast<unsigned int>(arePciConfigHooksInstalled()));
    }
    KernelLog::logDiagnosticInfo(KernelLog::Category::Hook, "TTL", "initialize raw", "Enter: parameters=%u context=%u",
                                 static_cast<unsigned int>(parameters != nullptr), static_cast<unsigned int>(context != nullptr));
    auto result = reinterpret_cast<uint32_t (*)(void *, void *)>(originalInitializeTtlRaw)(parameters, context);
    KernelLog::logDiagnosticInfo(KernelLog::Category::Hook, "TTL", "initialize raw", "Return: result=0x%08x", result);
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
    KernelPatcher::RouteRequest diagnosticRequests[] {
        {"__ZN14AmdTtlServices10initializeEv", wrapInitializeTtlServices, originalInitializeTtlServices},
        {"_ttl_initialize", wrapInitializeTtlRaw, originalInitializeTtlRaw},
    };
    for (auto &request : diagnosticRequests) {
        if (patcher.routeMultiple(index, &request, 1, address, size))
            KernelLog::logDiagnosticInfo(KernelLog::Category::Hook, "TTL", "install diagnostic", "Hook installed: %s", request.symbol);
        else KernelLog::logDiagnosticWarning(KernelLog::Category::Hook, "TTL", "install diagnostic", "Hook failed: %s error=%d", request.symbol, static_cast<int>(patcher.getError()));
        patcher.clearError();
    }
}

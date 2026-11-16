#include "Kexts/AMDRadeonX6000HWServices/AMDRadeonX6000HWLibs/HWLibsHooks.hpp"
#include "Diagnostics/KernelLog.hpp"

extern void applyGcGolden();

namespace {

using HookFunction = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
mach_vm_address_t originalGetGraphicsCoreVersion;
mach_vm_address_t originalApplyGoldenRegisters;
mach_vm_address_t originalInitializeGraphicsCoreRegisters;

uint64_t wrapGetGraphicsCoreVersion(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
    uint64_t version = reinterpret_cast<HookFunction>(originalGetGraphicsCoreVersion)(a, b, c, d, e, f) & 0xffffffff;
    return version == 0xa0303 ? 0xa0304 : version;
}

uint64_t wrapApplyGoldenRegisters(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
    uint64_t result = reinterpret_cast<HookFunction>(originalApplyGoldenRegisters)(a, b, c, d, e, f);
    applyGcGolden();
    return result;
}

uint64_t wrapInitializeGraphicsCoreRegisters(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
    uint64_t result = reinterpret_cast<HookFunction>(originalInitializeGraphicsCoreRegisters)(a, b, c, d, e, f);
    applyGcGolden();
    return result;
}

}

void installGraphicsCoreHooks(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
    KernelPatcher::RouteRequest requests[] {
        {"_gc_get_hw_version", wrapGetGraphicsCoreVersion, originalGetGraphicsCoreVersion},
        {"_execute_register_golden_settings", wrapApplyGoldenRegisters, originalApplyGoldenRegisters},
        {"_gc_10_3_initialize_registers", wrapInitializeGraphicsCoreRegisters, originalInitializeGraphicsCoreRegisters},
    };
    for (auto &request : requests) {
        if (patcher.routeMultiple(index, &request, 1, address, size))
            KernelLog::logInfo(KernelLog::Category::Hook, "HWLibs", "install graphics", "Hook installed: %s", request.symbol);
        else KernelLog::logError(KernelLog::Category::Hook, "HWLibs", "install graphics", "Hook failed: %s error=%d", request.symbol, static_cast<int>(patcher.getError()));
        patcher.clearError();
    }
}

void installHwLibsHooks(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
    KernelLog::logInfo(KernelLog::Category::Hook, "HWLibs", "install", "Installing HWLibs hooks");
    installPspRingHook(patcher, index, address, size);
    installGraphicsCoreHooks(patcher, index, address, size);
    installPspWakeHooks(patcher, index, address, size);
    installPowerPlayHooks(patcher, index, address, size);
    installPspAsicHook(patcher, index, address, size);
    installTtlDeviceHooks(patcher, index, address, size);
    resolvePspWakePost(patcher, index);
    resolvePowerPlayTasks(patcher, index);
    installSmuHooks(patcher, index, address, size);
    installFirmwareHooks(patcher, index, address, size);
    KernelLog::logInfo(KernelLog::Category::Hook, "HWLibs", "install", "HWLibs installation sequence complete");
}

#include "Kexts/AMDRadeonX6000HWServices/AMDRadeonX6000HWLibs/HWLibsHooks.hpp"
#include "Diagnostics/KernelLog.hpp"
#include <IOKit/IOLib.h>

extern uint32_t mmioRead(uint32_t reg);
extern void mmioWrite(uint32_t reg, uint32_t value);

namespace {

using HookFunction = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
mach_vm_address_t originalInitializePspAsicType;
mach_vm_address_t originalCreatePspRing;
mach_vm_address_t originalInitializeVbios;
mach_vm_address_t originalPspPowerUp;
mach_vm_address_t postVbios;
uint64_t vbios;
bool wakePostArmed;

uint64_t wrapInitializePspAsicType(uint64_t psp, uint64_t info, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
    auto version = reinterpret_cast<uint32_t *>(info);
    alignas(uint32_t) uint8_t copy[0x260];
    if (version[2] == 13) {
        memcpy(copy, version, sizeof(copy));
        auto replacement = reinterpret_cast<uint32_t *>(copy);
        replacement[2] = 11;
        replacement[3] = 0;
        replacement[4] = 11;
        info = reinterpret_cast<uint64_t>(copy);
    }
    return reinterpret_cast<HookFunction>(originalInitializePspAsicType)(psp, info, c, d, e, f);
}

uint64_t wrapCreatePspRing(uint64_t psp, uint64_t type, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
    if (type == 2) {
        constexpr uint32_t mailbox = 0x16000 + 0x80;
        mmioWrite(mailbox, 0x00030000);
        IODelay(20000);
        for (uint32_t i = 0; i < 2000 && !(mmioRead(mailbox) & 0x80000000); ++i) IODelay(1000);
    }
    return reinterpret_cast<HookFunction>(originalCreatePspRing)(psp, type, c, d, e, f);
}

uint64_t wrapInitializeVbios(uint64_t object, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
    vbios = object;
    return reinterpret_cast<HookFunction>(originalInitializeVbios)(object, b, c, d, e, f);
}

uint64_t wrapPspPowerUp(uint64_t psp, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
    if (wakePostArmed) {
        wakePostArmed = false;
        if (vbios && postVbios) reinterpret_cast<HookFunction>(postVbios)(vbios, 0, 0, 0, 0, 0);
    }
    return reinterpret_cast<HookFunction>(originalPspPowerUp)(psp, b, c, d, e, f);
}

}

void armHwLibsWakePost() {
    wakePostArmed = true;
}

void installPspRingHook(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
    KernelPatcher::RouteRequest request {"_psp_ring_create", wrapCreatePspRing, originalCreatePspRing};
    if (patcher.routeMultiple(index, &request, 1, address, size)) KernelLog::logInfo(KernelLog::Category::Hook, "PSP", "install", "Hook installed: %s", request.symbol);
    else KernelLog::logError(KernelLog::Category::Hook, "PSP", "install", "Hook failed: %s error=%d", request.symbol, static_cast<int>(patcher.getError()));
    patcher.clearError();
}

void installPspWakeHooks(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
    KernelPatcher::RouteRequest requests[] {
        {"_vbs_hw_init", wrapInitializeVbios, originalInitializeVbios},
        {"_psp_power_up_event", wrapPspPowerUp, originalPspPowerUp},
    };
    for (auto &request : requests) {
        if (patcher.routeMultiple(index, &request, 1, address, size)) KernelLog::logInfo(KernelLog::Category::Hook, "PSP", "install", "Hook installed: %s", request.symbol);
        else KernelLog::logError(KernelLog::Category::Hook, "PSP", "install", "Hook failed: %s error=%d", request.symbol, static_cast<int>(patcher.getError()));
        patcher.clearError();
    }
}

void installPspAsicHook(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
    KernelPatcher::RouteRequest request {"_psp_asic_type_init", wrapInitializePspAsicType, originalInitializePspAsicType};
    if (patcher.routeMultiple(index, &request, 1, address, size)) KernelLog::logInfo(KernelLog::Category::Hook, "PSP", "install", "Hook installed: %s", request.symbol);
    else KernelLog::logError(KernelLog::Category::Hook, "PSP", "install", "Hook failed: %s error=%d", request.symbol, static_cast<int>(patcher.getError()));
    patcher.clearError();
}

void resolvePspWakePost(KernelPatcher &patcher, size_t index) {
    postVbios = patcher.solveSymbol(index, "_vbs_post");
    if (!postVbios) KernelLog::logWarning(KernelLog::Category::Hook, "PSP", "resolve", "VBIOS POST unavailable: error=%d", static_cast<int>(patcher.getError()));
    patcher.clearError();
}

#include "Kexts/AMDRadeonX6000/AcceleratorHooks.hpp"
#include "Diagnostics/KernelLog.hpp"

void installAcceleratorHooks(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
    getHardwareChannelByEngine = patcher.solveSymbol(index,
        "__ZN26AMDRadeonX6000_AMDHardware12getHWChannelE20_eAMD_HW_ENGINE_TYPE18_eAMD_HW_RING_TYPE", address, size);
    if (!getHardwareChannelByEngine) KernelLog::logWarning(KernelLog::Category::Hook, "X6000", "resolve", "Hardware channel lookup unavailable: error=%d", static_cast<int>(patcher.getError()));
    patcher.clearError();

    KernelPatcher::RouteRequest requests[] {
        {"__ZN26AMDRadeonX6000_AMDHardware18updateHWInfoClocksEv", wrapUpdateHardwareClocks, originalUpdateHardwareClocks},
        {"__ZNK36AMDRadeonX6000_AMDAccelResourceAddr221shouldAllocScanoutDccEjjjj", wrapScanoutDcc},
        {"__ZN30AMDRadeonX6000_AMDKIQHWChannel14submitKIQFrameEv", wrapSubmitKiqFrame, originalSubmitKiqFrame},
        {"__ZN35AMDRadeonX6000_AMDGFX10KIQHWChannel8startKIQEyyPK18AMD_PM4_QUEUE_SPECPj", wrapStartKiq},
        {"__ZN26AMDRadeonX6000_AMDHWMemory17adjustVRAMAddressEy", wrapAdjustVramAddress},
        {"__ZN26AMDRadeonX6000_AMDHardware16powerUpHWEnginesEv", wrapPowerUpHardwareEngines},
        {"__ZN28AMDRadeonX6000_AMDRTHardware12getHWChannelE18_eAMD_CHANNEL_TYPE11SS_PRIORITYj", wrapGetRtHardwareChannel, originalGetRtHardwareChannel},
        {"__ZN26AMDRadeonX6000_AMDHardware14startHWEnginesEv", wrapStartHardwareEngines},
        {"__ZN33AMDRadeonX6000_AMDGFX10SDMAEngine5resetE18_eAMD_HW_RING_TYPEPj", wrapResetSdma, originalResetSdma},
        {"__ZN26AMDRadeonX6000_AMDHardware13resetHardwareEjPj", wrapResetHardware, originalResetHardware},
    };
    for (auto &request : requests) {
        if (patcher.routeMultiple(index, &request, 1, address, size))
            KernelLog::logInfo(KernelLog::Category::Hook, "X6000", "install", "Hook installed: %s", request.symbol);
        else KernelLog::logError(KernelLog::Category::Hook, "X6000", "install", "Hook failed: %s error=%d", request.symbol, static_cast<int>(patcher.getError()));
        patcher.clearError();
    }
}

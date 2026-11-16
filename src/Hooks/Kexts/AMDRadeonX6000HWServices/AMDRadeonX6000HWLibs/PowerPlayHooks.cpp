#include "Kexts/AMDRadeonX6000HWServices/AMDRadeonX6000HWLibs/HWLibsHooks.hpp"
#include "Diagnostics/KernelLog.hpp"

namespace {

using PowerTask = uint32_t (*)(void *, void *);
mach_vm_address_t skipDynamicStateTask;
mach_vm_address_t toleratePowerSourceTask;

uint64_t wrapExecutePowerEventChain(uint64_t context, uint64_t chain, uint64_t data, uint64_t, uint64_t, uint64_t) {
    if (!chain || !data) return 7;
    auto lists = *reinterpret_cast<PowerTask ***>(chain + 0x10);
    uint32_t result = 1;
    for (; lists && *lists && result == 1; ++lists) {
        for (auto task = *lists; *task; ++task) {
            auto address = reinterpret_cast<mach_vm_address_t>(*task);
            uint32_t taskResult = skipDynamicStateTask && address == skipDynamicStateTask ? 1 :
                (*task)(reinterpret_cast<void *>(context), reinterpret_cast<void *>(data));
            if (taskResult != 1 && toleratePowerSourceTask && address == toleratePowerSourceTask) taskResult = 1;
            if (result == 1) result = taskResult;
        }
    }
    return result;
}

uint64_t wrapApplyClockAdjustmentRules(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) {
    return 1;
}

uint64_t wrapInitializePcieIp(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) {
    return 0;
}

}

void installPowerPlayHooks(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
    KernelPatcher::RouteRequest requests[] {
        {"_pcie_ip_sw_init", wrapInitializePcieIp},
        {"_PEM_ExcuteEventChain", wrapExecutePowerEventChain},
        {"_Phw_CommonDynamicState_ApplyClocksAdjustRules", wrapApplyClockAdjustmentRules},
    };
    for (auto &request : requests) {
        if (patcher.routeMultiple(index, &request, 1, address, size))
            KernelLog::logInfo(KernelLog::Category::Hook, "PowerPlay", "install", "Hook installed: %s", request.symbol);
        else KernelLog::logError(KernelLog::Category::Hook, "PowerPlay", "install", "Hook failed: %s error=%d", request.symbol, static_cast<int>(patcher.getError()));
        patcher.clearError();
    }
}

void resolvePowerPlayTasks(KernelPatcher &patcher, size_t index) {
    skipDynamicStateTask = patcher.solveSymbol(index, "_PEM_Task_EnableDynamicStateManagement");
    if (!skipDynamicStateTask) KernelLog::logWarning(KernelLog::Category::Hook, "PowerPlay", "resolve", "Dynamic state task unavailable: error=%d", static_cast<int>(patcher.getError()));
    patcher.clearError();
    toleratePowerSourceTask = patcher.solveSymbol(index, "_PEM_Task_NotifyHWOfPowerSource");
    if (!toleratePowerSourceTask) KernelLog::logWarning(KernelLog::Category::Hook, "PowerPlay", "resolve", "Power source task unavailable: error=%d", static_cast<int>(patcher.getError()));
    patcher.clearError();
}

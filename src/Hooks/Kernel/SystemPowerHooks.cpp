#include "Kernel/SystemPowerHooks.hpp"
#include "Diagnostics/KernelLog.hpp"
#include <IOKit/IOService.h>

namespace {

using UpdateSleepPreventionList = void (*)(void *, IOService *, bool);
mach_vm_address_t originalUpdateSleepPreventionList;

void wrapUpdateSleepPreventionList(void *root, IOService *service, bool add) {
    if (add && service && service->metaCast("IONDRVFramebuffer")) return;
    reinterpret_cast<UpdateSleepPreventionList>(originalUpdateSleepPreventionList)(root, service, add);
}

}

void installSystemPowerHooks(KernelPatcher &patcher) {
    KernelPatcher::RouteRequest request {
        "__ZN14IOPMrootDomain28updatePreventSystemSleepListEP9IOServiceb",
        wrapUpdateSleepPreventionList, originalUpdateSleepPreventionList
    };
    if (patcher.routeMultiple(KernelPatcher::KernelID, &request, 1))
        KernelLog::logInfo(KernelLog::Category::Hook, "SystemPower", "install", "Sleep prevention hook installed");
    else KernelLog::logError(KernelLog::Category::Hook, "SystemPower", "install", "Sleep prevention hook failed: error=%d", static_cast<int>(patcher.getError()));
    patcher.clearError();
}

#include "Kernel/DriverMatchingHooks.hpp"
#include "Diagnostics/KernelLog.hpp"
#include <Headers/kern_patcher.hpp>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSString.h>
#include <libkern/libkern.h>

namespace {

mach_vm_address_t originalAddDrivers;

bool isTarget(const OSString *bundle, const OSString *driverClass) {
    if (!bundle || !driverClass) return false;
    if (bundle->isEqualTo("com.apple.kext.AMDRadeonX6000Framebuffer"))
        return driverClass->isEqualTo("AMDRadeonX6000_AmdGpuWrangler") ||
            driverClass->isEqualTo("AMDRadeonX6000_AmdRadeonControllerNavi23");
    if (bundle->isEqualTo("com.apple.kext.AMDRadeonX6000"))
        return driverClass->isEqualTo("AMDRadeonX6000_AMDNavi23GraphicsAccelerator");
    if (bundle->isEqualTo("com.apple.kext.AMDRadeonX6000HWServices"))
        return driverClass->isEqualTo("AMDRadeonX6000_AMDRadeonHWServicesNavi");
    return false;
}

bool wrapAddDrivers(void *catalogue, OSArray *drivers, bool match) {
    if (drivers) {
        unsigned int count = drivers->getCount();
        for (unsigned int i = 0; i < count; ++i) {
            auto entry = OSDynamicCast(OSDictionary, drivers->getObject(i));
            if (!entry) continue;
            auto bundle = OSDynamicCast(OSString, entry->getObject("CFBundleIdentifier"));
            auto driverClass = OSDynamicCast(OSString, entry->getObject("IOClass"));
            auto pciMatch = OSDynamicCast(OSString, entry->getObject("IOPCIMatch"));
            if (!isTarget(bundle, driverClass) || !pciMatch) continue;
            char matches[2048];
            int written = snprintf(matches, sizeof(matches), "%s 0x16811002 0x164d1002", pciMatch->getCStringNoCopy());
            if (written < 0 || static_cast<size_t>(written) >= sizeof(matches)) continue;
            auto replacement = OSDictionary::withDictionary(entry);
            auto value = OSString::withCString(matches);
            if (replacement && value && replacement->setObject("IOPCIMatch", value)) {
                KernelLog::logInfo(KernelLog::Category::Hook, "DriverMatching", "extend", "Added Rembrandt IDs to %s", driverClass->getCStringNoCopy());
                drivers->replaceObject(i, replacement);
            } else {
                KernelLog::logError(KernelLog::Category::Hook, "DriverMatching", "extend", "Unable to update driver matching");
            }
            if (value) value->release();
            if (replacement) replacement->release();
        }
    }
    return reinterpret_cast<bool (*)(void *, OSArray *, bool)>(originalAddDrivers)(catalogue, drivers, match);
}

}

void installDriverMatchingHooks(KernelPatcher &patcher) {
    KernelPatcher::RouteRequest request {"__ZN11IOCatalogue10addDriversEP7OSArrayb", wrapAddDrivers, originalAddDrivers};
    if (patcher.routeMultipleLong(KernelPatcher::KernelID, &request, 1))
        KernelLog::logInfo(KernelLog::Category::Hook, "DriverMatching", "install", "Driver matching hook installed");
    else
        KernelLog::logError(KernelLog::Category::Hook, "DriverMatching", "install", "Driver matching hook failed: error=%d", static_cast<int>(patcher.getError()));
    patcher.clearError();
}

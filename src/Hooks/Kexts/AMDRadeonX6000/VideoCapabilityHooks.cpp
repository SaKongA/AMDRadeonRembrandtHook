#include "Kexts/AMDRadeonX6000/AcceleratorHooks.hpp"
#include "Diagnostics/KernelLog.hpp"
#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>

namespace {

bool isVideoCapability(const char *key) {
    return !strncmp(key, "IOGVA", 5) || !strcmp(key, "IOVARendererID") ||
           !strcmp(key, "IOVARendererSubID") || !strcmp(key, "IODVDBundleName");
}

void removeVideoProperties(IORegistryEntry *accelerator) {
    auto properties = accelerator->dictionaryWithProperties();
    auto iterator = properties ? OSCollectionIterator::withCollection(properties) : nullptr;
    while (auto key = iterator ? OSDynamicCast(OSSymbol, iterator->getNextObject()) : nullptr) {
        if (isVideoCapability(key->getCStringNoCopy())) accelerator->removeProperty(key);
    }
    OSSafeReleaseNULL(iterator);
    OSSafeReleaseNULL(properties);
}

}

void removeVideoCapabilities(void *, wait_result_t) {
    constexpr uint64_t timeout = 120ULL * 1000 * 1000 * 1000;
    auto matching = IOService::serviceMatching("AMDRadeonX6000_AMDNavi23GraphicsAccelerator");
    auto accelerator = matching ? IOService::waitForMatchingService(matching, timeout) : nullptr;
    OSSafeReleaseNULL(matching);
    if (!accelerator) {
        KernelLog::logWarning(KernelLog::Category::Hook, "Video", "remove capabilities", "Accelerator not found within timeout");
        return;
    }
    KernelLog::logInfo(KernelLog::Category::Hook, "Video", "remove capabilities", "Starting accelerator capability cleanup");
    removeVideoProperties(accelerator);
    for (int i = 0; i < 180; ++i) {
        IOSleep(1000);
        removeVideoProperties(accelerator);
    }
    accelerator->release();
    KernelLog::logInfo(KernelLog::Category::Hook, "Video", "remove capabilities", "Accelerator capability cleanup complete");
}

#include "Kernel/GpuInterruptStartup.hpp"
#include "Diagnostics/KernelLog.hpp"
#include <IOKit/IOService.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <libkern/c++/OSData.h>

extern void quiesceIh(const char *reason);

namespace {

IONotifier *pciNotifier;

uint32_t getRegistryUint32(IORegistryEntry *entry, const char *key) {
    auto data = OSDynamicCast(OSData, entry->getProperty(key));
    return data && data->getLength() >= sizeof(uint32_t) ? *static_cast<const uint32_t *>(data->getBytesNoCopy()) : 0;
}

bool handleGpuPublished(void *, void *, IOService *service, IONotifier *) {
    if (getRegistryUint32(service, "vendor-id") != 0x1002 || (getRegistryUint32(service, "class-code") >> 16) != 0x03)
        return true;
    // The notification matches IOPCIDevice; avoid importing its metaClass from IOPCIFamily.
    auto device = static_cast<IOPCIDevice *>(service);
    IOPCIAddressSpace address {};
    address.bits = getRegistryUint32(service, "reg") & 0x00ffff00;
    uint16_t command = device->configRead16(address, static_cast<UInt8>(kIOPCIConfigCommand));
    device->configWrite16(address, static_cast<UInt8>(kIOPCIConfigCommand), command | kIOPCICommandMemorySpace);
    quiesceIh(nullptr);
    uint16_t status = device->configRead16(address, static_cast<UInt8>(kIOPCIConfigStatus));
    KernelLog::logInfo(KernelLog::Category::Hook, "GpuInterrupt", "cleanup", "AMD display device PCI status=0x%04x", static_cast<unsigned int>(status));
    if (status & 0x8) command |= kIOPCICommandInterruptDisable;
    device->configWrite16(address, static_cast<UInt8>(kIOPCIConfigCommand), command);
    return true;
}

}

bool startGpuInterruptCleanup() {
    if (pciNotifier) return true;
    auto matching = IOService::serviceMatching("IOPCIDevice");
    if (!matching) {
        KernelLog::logError(KernelLog::Category::Hook, "GpuInterrupt", "register", "Unable to create PCI matching dictionary");
        return false;
    }
    pciNotifier = IOService::addMatchingNotification(gIOFirstPublishNotification, matching, handleGpuPublished, nullptr);
    matching->release();
    if (!pciNotifier) KernelLog::logError(KernelLog::Category::Hook, "GpuInterrupt", "register", "PCI publication notification failed");
    else KernelLog::logInfo(KernelLog::Category::Hook, "GpuInterrupt", "register", "PCI publication notification installed");
    return pciNotifier != nullptr;
}

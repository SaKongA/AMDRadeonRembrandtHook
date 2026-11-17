#include "Kernel/GpuInterruptStartup.hpp"
#include "Diagnostics/KernelLog.hpp"
#include "Hardware/Rembrandt/PciIdentity.hpp"
#include "Hardware/Rembrandt/Vbios.hpp"
#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <libkern/c++/OSData.h>

extern void quiesceIh(const char *reason);

namespace {

IONotifier *pciNotifier;
IONotifier *acceleratorNotifier;
IOPCIDevice *rembrandtDevice;
uint32_t rembrandtDeviceId;
IOLock *resourcesLock;
bool resourcesPrepared;

uint32_t getRegistryUint32(IORegistryEntry *entry, const char *key) {
    auto data = OSDynamicCast(OSData, entry->getProperty(key));
    return data && data->getLength() >= sizeof(uint32_t) ? *static_cast<const uint32_t *>(data->getBytesNoCopy()) : 0;
}

bool handleAcceleratorMatched(void *, void *, IOService *service, IONotifier *) {
    auto provider = service->getProvider();
    IOLockLock(resourcesLock);
    bool selected = provider == rembrandtDevice && isRembrandtDeviceId(rembrandtDeviceId);
    IOLockUnlock(resourcesLock);
    if (selected) publishRembrandtModel(provider);
    return true;
}

bool handleGpuPublished(void *, void *, IOService *service, IONotifier *) {
    if (getRegistryUint32(service, "vendor-id") != 0x1002 || (getRegistryUint32(service, "class-code") >> 16) != 0x03)
        return true;
    auto device = static_cast<IOPCIDevice *>(service);
    if (isRembrandtDeviceId(getRegistryUint32(service, "device-id"))) {
        IOLockLock(resourcesLock);
        bool selected = false;
        if (!rembrandtDevice) {
            device->retain();
            rembrandtDevice = device;
            rembrandtDeviceId = getRegistryUint32(service, "device-id");
            device->removeProperty("ATY,bin_image");
            selected = true;
        }
        IOLockUnlock(resourcesLock);
        if (selected) {
            IOReturn result = device->getResources();
            if (result == kIOReturnSuccess) prepareRembrandtGpuResources(device);
            else KernelLog::logError(KernelLog::Category::Hardware, "VBIOS", "prepare", "GPU resources unavailable: 0x%x", static_cast<unsigned int>(result));
        }
    }
    publishRembrandtIdentity(service, getRegistryUint32(service, "device-id"));
    return true;
}

}

void prepareRembrandtGpuResources(IOPCIDevice *device) {
    if (!resourcesLock) return;
    IOLockLock(resourcesLock);
    if (device != rembrandtDevice || !isRembrandtDeviceId(rembrandtDeviceId) || resourcesPrepared) {
        IOLockUnlock(resourcesLock);
        return;
    }
    if (!device->getDeviceMemoryWithRegister(kIOPCIConfigBaseAddress5)) {
        KernelLog::logError(KernelLog::Category::Hardware, "VBIOS", "prepare", "GPU register aperture unavailable");
        IOLockUnlock(resourcesLock);
        return;
    }
    IOPCIAddressSpace address {};
    address.bits = getRegistryUint32(device, "reg") & 0x00ffff00;
    uint16_t command = device->configRead16(address, static_cast<UInt8>(kIOPCIConfigCommand));
    device->configWrite16(address, static_cast<UInt8>(kIOPCIConfigCommand), command | kIOPCICommandMemorySpace);
    if (!prepareRembrandtVbios(device))
        KernelLog::logError(KernelLog::Category::Hardware, "VBIOS", "prepare", "Unable to prepare VBIOS from VFCT");
    quiesceIh(nullptr);
    uint16_t status = device->configRead16(address, static_cast<UInt8>(kIOPCIConfigStatus));
    KernelLog::logInfo(KernelLog::Category::Hook, "GpuInterrupt", "cleanup", "AMD display device PCI status=0x%04x", static_cast<unsigned int>(status));
    if (status & 0x8) command |= kIOPCICommandInterruptDisable;
    device->configWrite16(address, static_cast<UInt8>(kIOPCIConfigCommand), command);
    resourcesPrepared = true;
    IOLockUnlock(resourcesLock);
}

bool startGpuInterruptCleanup() {
    if (pciNotifier) return true;
    if (!resourcesLock) resourcesLock = IOLockAlloc();
    if (!resourcesLock) return false;
    auto matching = IOService::serviceMatching("IOPCIDevice");
    if (!matching) {
        KernelLog::logError(KernelLog::Category::Hook, "GpuInterrupt", "register", "Unable to create PCI matching dictionary");
        return false;
    }
    pciNotifier = IOService::addMatchingNotification(gIOFirstPublishNotification, matching, handleGpuPublished, nullptr);
    matching->release();
    if (!pciNotifier) KernelLog::logError(KernelLog::Category::Hook, "GpuInterrupt", "register", "PCI publication notification failed");
    else KernelLog::logInfo(KernelLog::Category::Hook, "GpuInterrupt", "register", "PCI publication notification installed");
    auto acceleratorMatching = IOService::serviceMatching("AMDRadeonX6000_AMDNavi23GraphicsAccelerator");
    if (acceleratorMatching) {
        acceleratorNotifier = IOService::addMatchingNotification(gIOMatchedNotification, acceleratorMatching, handleAcceleratorMatched, nullptr);
        acceleratorMatching->release();
    }
    if (!acceleratorNotifier) KernelLog::logError(KernelLog::Category::Hardware, "PCI", "publish model", "Accelerator notification unavailable");
    return pciNotifier != nullptr;
}

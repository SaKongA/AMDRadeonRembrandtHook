#include "Hardware/USB4/Usb4Power.hpp"
#include "Diagnostics/KernelLog.hpp"
#include "Hardware/USB4/Usb4Nhi.hpp"
#include <IOKit/IOMessage.h>
#include <IOKit/IOService.h>
#include <IOKit/pwr_mgt/IOPM.h>
#include <IOKit/pwr_mgt/RootDomain.h>
#include <kern/thread.h>
#include <libkern/c++/OSData.h>

namespace {
IONotifier *pciNotifier;
IONotifier *powerNotifier;

uint32_t getRegistryUint32(IORegistryEntry *entry, const char *key) {
    auto data = OSDynamicCast(OSData, entry->getProperty(key));
    return data && data->getLength() >= 4 ? *static_cast<const uint32_t *>(data->getBytesNoCopy()) : 0;
}

bool isSleepEntry(UInt32 type, void *argument) {
    if (type == kIOMessageSystemWillSleep) return true;
    auto change = static_cast<const IOPMSystemCapabilityChangeParameters *>(argument);
    return type == kIOMessageSystemCapabilityChange && change &&
        (change->changeFlags & kIOPMSystemCapabilityWillChange) &&
        (change->fromCapabilities & kIOPMSystemCapabilityCPU) && !(change->toCapabilities & kIOPMSystemCapabilityCPU);
}

IOReturn handleUsb4PowerEvent(void *, void *, UInt32 type, IOService *, void *argument, vm_size_t) {
    if (isSleepEntry(type, argument)) {
        if (runUsb4Router(true)) KernelLog::logInfo(KernelLog::Category::Hardware, "USB4", "sleep", "Router sleep request completed");
        else KernelLog::logWarning(KernelLog::Category::Hardware, "USB4", "sleep", "Router sleep request failed");
    }
    return kIOReturnSuccess;
}

void probeUsb4Router(void *, wait_result_t) {
    IOSleep(20000);
    if (runUsb4Router(false)) KernelLog::logInfo(KernelLog::Category::Hardware, "USB4", "probe", "Router configuration read succeeded");
    else KernelLog::logWarning(KernelLog::Category::Hardware, "USB4", "probe", "Router configuration read failed");
    thread_terminate(current_thread());
}

bool handleUsb4DevicePublished(void *, void *, IOService *service, IONotifier *) {
    if (getRegistryUint32(service, "vendor-id") != 0x1022 || getRegistryUint32(service, "class-code") != 0x0c0340) return true;
    auto device = static_cast<IOPCIDevice *>(service);
    IOPCIAddressSpace address {};
    address.bits = getRegistryUint32(service, "reg") & 0x00ffff00;
    if (!attachUsb4Nhi(device, address)) return true;
    KernelLog::logInfo(KernelLog::Category::Hardware, "USB4", "attach", "AMD USB4 controller attached: PCI=0x%08x", static_cast<unsigned int>(address.bits));
    powerNotifier = registerPrioritySleepWakeInterest(handleUsb4PowerEvent, nullptr, nullptr);
    if (!powerNotifier) KernelLog::logError(KernelLog::Category::Hardware, "USB4", "register power", "Power notification registration failed");
    thread_t thread;
    if (kernel_thread_start(probeUsb4Router, nullptr, &thread) == KERN_SUCCESS) thread_deallocate(thread);
    else KernelLog::logError(KernelLog::Category::Hardware, "USB4", "probe", "Unable to start router probe worker");
    return true;
}
}

bool startUsb4PowerManagement() {
    if (pciNotifier) return true;
    if (!initializeUsb4Nhi()) {
        KernelLog::logError(KernelLog::Category::Hardware, "USB4", "initialize", "Unable to initialize NHI lock");
        return false;
    }
    auto matching = IOService::serviceMatching("IOPCIDevice");
    if (!matching) {
        KernelLog::logError(KernelLog::Category::Hardware, "USB4", "register", "Unable to create PCI matching dictionary");
        return false;
    }
    pciNotifier = IOService::addMatchingNotification(gIOFirstPublishNotification, matching, handleUsb4DevicePublished, nullptr);
    matching->release();
    if (!pciNotifier) KernelLog::logError(KernelLog::Category::Hardware, "USB4", "register", "PCI publication notification failed");
    else KernelLog::logInfo(KernelLog::Category::Hardware, "USB4", "register", "PCI publication notification installed");
    return pciNotifier != nullptr;
}

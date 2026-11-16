#include "Hardware/Rembrandt/PciIdentity.hpp"
#include "Hardware/Rembrandt/Vbios.hpp"
#include "Diagnostics/KernelLog.hpp"
#include <IOKit/IORegistryEntry.h>
#include <libkern/c++/OSData.h>

bool publishRembrandtIdentity(IORegistryEntry *device, uint32_t deviceId) {
    if (!device || !isRembrandtDeviceId(deviceId)) return false;
    publishRembrandtVbios(device, deviceId);
    const uint32_t spoofedDeviceId = 0x73ff;
    auto data = OSData::withBytes(&spoofedDeviceId, sizeof(spoofedDeviceId));
    bool published = data && device->setProperty("device-id", data);
    if (data) data->release();
    KernelLog::logInfo(KernelLog::Category::Hardware, "PCI", "publish identity", "Device %04x -> %04x published=%u", deviceId, spoofedDeviceId, static_cast<unsigned int>(published));
    return published;
}

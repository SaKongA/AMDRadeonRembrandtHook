#pragma once

#include <stdint.h>

class IORegistryEntry;

constexpr bool isRembrandtDeviceId(uint32_t deviceId) {
    return deviceId == 0x1681 || deviceId == 0x164d;
}

bool publishRembrandtIdentity(IORegistryEntry *device, uint32_t deviceId);

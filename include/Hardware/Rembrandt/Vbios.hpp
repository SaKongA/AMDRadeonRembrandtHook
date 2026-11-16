#pragma once

#include <stdint.h>

class IORegistryEntry;
bool publishRembrandtVbios(IORegistryEntry *device, uint32_t deviceId);

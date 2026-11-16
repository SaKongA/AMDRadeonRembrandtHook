#pragma once

#include <IOKit/pci/IOPCIDevice.h>

bool initializeUsb4Nhi();
bool attachUsb4Nhi(IOPCIDevice *device, IOPCIAddressSpace address);
bool runUsb4Router(bool sleep);

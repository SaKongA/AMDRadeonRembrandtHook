#pragma once

class IOPCIDevice;

bool startGpuInterruptCleanup();
void prepareRembrandtGpuResources(IOPCIDevice *device);

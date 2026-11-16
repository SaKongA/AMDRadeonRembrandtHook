#pragma once

#include <Headers/kern_patcher.hpp>

using AcceleratorHookFunction = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

extern volatile bool gKiqSleepSkip;
extern mach_vm_address_t originalSubmitKiqFrame;
extern mach_vm_address_t originalGetRtHardwareChannel;
extern mach_vm_address_t getHardwareChannelByEngine;
extern mach_vm_address_t originalUpdateHardwareClocks;
extern mach_vm_address_t originalResetSdma;
extern mach_vm_address_t originalResetHardware;

uint64_t wrapUpdateHardwareClocks(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t wrapScanoutDcc(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t wrapAdjustVramAddress(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t wrapSubmitKiqFrame(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t wrapStartKiq(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t wrapPowerUpHardwareEngines(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t wrapGetRtHardwareChannel(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t wrapStartHardwareEngines(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t wrapResetSdma(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t wrapResetHardware(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
void installAcceleratorHooks(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
void removeVideoCapabilities(void *, wait_result_t);

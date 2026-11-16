#pragma once

#include <Headers/kern_patcher.hpp>

void installHwLibsHooks(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
void installTtlDeviceHooks(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
void installPspRingHook(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
void installPspWakeHooks(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
void installPspAsicHook(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
void resolvePspWakePost(KernelPatcher &patcher, size_t index);
void installPowerPlayHooks(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
void resolvePowerPlayTasks(KernelPatcher &patcher, size_t index);
void installGraphicsCoreHooks(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
void installSmuHooks(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
void installFirmwareHooks(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
void armHwLibsWakePost();

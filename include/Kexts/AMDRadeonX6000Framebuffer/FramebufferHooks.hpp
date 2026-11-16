#pragma once

#include <Headers/kern_patcher.hpp>

void installFramebufferIdentityHooks(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
void installDisplayModeHooks(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
void patchFramebufferCapabilities(mach_vm_address_t address, size_t size);
void installFramebufferBrightnessHooks(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
bool initializeDalRegisterHooks();
void installDalRegisterHooks(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
void installRawRegisterHooks(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
void installDisplayStreamHook(KernelPatcher &patcher, mach_vm_address_t address, size_t size);
void setDisplayRecoveryAgdcAddress(mach_vm_address_t address);
void armPanelWakeRecovery();
void startPanelWakeRecovery();
void prepareDisplayRecovery(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
void installDisplayRecoveryHook(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
void installDisplayPresenterHooks(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
void installInterruptRingHooks(KernelPatcher &patcher, mach_vm_address_t address, size_t size);

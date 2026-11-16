#pragma once

#include <Headers/kern_patcher.hpp>

void installAppleBacklightPanelHook(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);

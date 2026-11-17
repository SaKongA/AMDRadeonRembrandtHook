#pragma once

#include <Headers/kern_patcher.hpp>

bool installPciConfigHooks(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
bool arePciConfigHooksInstalled();

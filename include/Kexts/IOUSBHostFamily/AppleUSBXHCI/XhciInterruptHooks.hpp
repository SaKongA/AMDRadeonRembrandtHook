#pragma once

#include <Headers/kern_patcher.hpp>

void patchXhciInterruptTargets(mach_vm_address_t address, size_t size);

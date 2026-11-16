#pragma once

#include <stdint.h>

void setPanelPowerOffAllowed(bool allowed);
void markPanelPowerLost();
void powerOffPanel();
uint32_t filterDalDmubCommands(uint32_t oldPointer, uint32_t newPointer);

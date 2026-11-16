#pragma once

#include <stdint.h>

void savePanelPowerSequence();
bool revivePanelPowerSequence();
void setBacklight(uint32_t level);
void setBacklightFromDal(uint32_t level);

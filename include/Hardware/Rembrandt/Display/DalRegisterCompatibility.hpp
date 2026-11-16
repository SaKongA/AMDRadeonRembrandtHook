#pragma once

#include <stdint.h>

bool isDalPhyRegister(uint32_t reg);
bool isUnavailableDalPhyRegister(uint32_t reg);
uint32_t readDalPowerGatingRegister(uint32_t reg);
void writeDalPowerGatingRegister(uint32_t reg, uint32_t value);
bool isDalPowerGatingRegister(uint32_t reg);
uint32_t translateDalRegisterRead(uint32_t reg, uint32_t value);
uint32_t translateDalRegisterWrite(uint32_t reg, uint32_t value, uint32_t previous);
uint32_t prepareDalRegisterWrite(uint32_t reg, uint32_t value);

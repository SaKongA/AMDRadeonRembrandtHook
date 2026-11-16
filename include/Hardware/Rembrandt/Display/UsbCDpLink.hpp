#pragma once
#include <stdint.h>
uint32_t adjustUsbCDpRead(uint32_t reg, uint32_t value);
uint64_t adjustUsbCDpWrite(uint32_t reg, uint64_t value);
void prepareUsbCDpWrite(uint32_t reg, uint32_t value);
void prepareUsbCDpCommands(uint32_t writePointer);

#pragma once

#include <stdint.h>

uint32_t sendSmuMessage(uint32_t message, uint32_t &argument, uint32_t timeoutMs = 1000);
void haltGraphicsEngines();
void resetGraphicsEngines();

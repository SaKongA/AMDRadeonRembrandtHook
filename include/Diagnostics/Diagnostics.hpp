#pragma once

#include <stdint.h>

void logInterruptDiagnostics(const char *reason, const uint32_t *controls, uint32_t count);
void logGpuVmDiagnostics(const char *operation, uint64_t tableOffset, uint64_t entryCount, bool valid);
void logVramSaveDiagnostics(uint64_t entryCount, uint64_t zeroCount, uint64_t allOnesCount);
void logVramRestoreDiagnostics(uint64_t checkedCount, uint64_t emptyCount, uint64_t mismatchCount,
                               uint64_t allOnesCount, uint64_t writeFailureCount);
void logDisplayRecoveryDiagnostics(const char *operation, uint32_t attempt, uint64_t result, uint64_t elapsedMicroseconds);
void logDisplayLinkDiagnostics(uint32_t bandwidth, uint32_t lanes, bool success, uint32_t failures);

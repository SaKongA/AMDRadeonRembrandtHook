#include "Diagnostics/Diagnostics.hpp"
#include "Diagnostics/KernelLog.hpp"

void logDisplayRecoveryDiagnostics(const char *operation, uint32_t attempt, uint64_t result, uint64_t elapsedMicroseconds) {
    KernelLog::logDiagnosticInfo(KernelLog::Category::Hardware, "DisplayTrace", operation,
                       "attempt=%u result=0x%llx elapsed-us=%llu", attempt,
                       static_cast<unsigned long long>(result), static_cast<unsigned long long>(elapsedMicroseconds));
}

void logDisplayLinkDiagnostics(uint32_t bandwidth, uint32_t lanes, bool success, uint32_t failures) {
    if (success) {
        KernelLog::logDiagnosticInfo(KernelLog::Category::Hardware, "DisplayTrace", "retrain panel link",
                           "bandwidth=0x%02x lanes=%u result=success", bandwidth, lanes);
    } else {
        KernelLog::logDiagnosticWarning(KernelLog::Category::Hardware, "DisplayTrace", "retrain panel link",
                              "bandwidth=0x%02x lanes=%u failures=%u", bandwidth, lanes, failures);
    }
}

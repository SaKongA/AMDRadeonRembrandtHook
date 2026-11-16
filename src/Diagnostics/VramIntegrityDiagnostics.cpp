#include "Diagnostics/Diagnostics.hpp"
#include "Diagnostics/KernelLog.hpp"

void logVramSaveDiagnostics(uint64_t entryCount, uint64_t zeroCount, uint64_t allOnesCount) {
    KernelLog::logDiagnosticInfo(KernelLog::Category::Hardware, "VramIntegrity", "save GART",
                       "entries=%llu zero=%llu all-ones=%llu; all-ones may indicate an unavailable read",
                       static_cast<unsigned long long>(entryCount), static_cast<unsigned long long>(zeroCount),
                       static_cast<unsigned long long>(allOnesCount));
}

void logVramRestoreDiagnostics(uint64_t checkedCount, uint64_t emptyCount, uint64_t mismatchCount,
                               uint64_t allOnesCount, uint64_t writeFailureCount) {
    if (writeFailureCount || mismatchCount || allOnesCount) {
        KernelLog::logDiagnosticWarning(KernelLog::Category::Hardware, "VramIntegrity", "restore GART",
                              "checked=%llu empty=%llu differing=%llu all-ones=%llu write-failures=%llu",
                              static_cast<unsigned long long>(checkedCount), static_cast<unsigned long long>(emptyCount),
                              static_cast<unsigned long long>(mismatchCount), static_cast<unsigned long long>(allOnesCount),
                              static_cast<unsigned long long>(writeFailureCount));
    } else {
        KernelLog::logDiagnosticInfo(KernelLog::Category::Hardware, "VramIntegrity", "restore GART",
                           "checked=%llu empty=%llu write-failures=0", static_cast<unsigned long long>(checkedCount),
                           static_cast<unsigned long long>(emptyCount));
    }
}

#include "Diagnostics/Diagnostics.hpp"
#include "Diagnostics/KernelLog.hpp"

void logGpuVmDiagnostics(const char *operation, uint64_t tableOffset, uint64_t entryCount, bool valid) {
    if (!valid) {
        KernelLog::logDiagnosticWarning(KernelLog::Category::Hardware, "GpuVmDiagnostics", operation,
                              "GART table geometry unavailable or changed");
        return;
    }
    KernelLog::logDiagnosticInfo(KernelLog::Category::Hardware, "GpuVmDiagnostics", operation,
                       "GART table-offset=0x%llx entries=%llu",
                       static_cast<unsigned long long>(tableOffset), static_cast<unsigned long long>(entryCount));
}

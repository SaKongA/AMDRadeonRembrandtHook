#include "Diagnostics/Diagnostics.hpp"
#include "Diagnostics/KernelLog.hpp"

void logInterruptDiagnostics(const char *reason, const uint32_t *controls, uint32_t count) {
    if (count != 3) {
        KernelLog::logDiagnosticWarning(KernelLog::Category::Hardware, "InterruptDiagnostics", "quiesce",
                              "reason=%s rings-cleared=%u of 3; register unavailable", reason ? reason : "startup", count);
        return;
    }
    KernelLog::logDiagnosticInfo(KernelLog::Category::Hardware, "InterruptDiagnostics", "quiesce",
                       "reason=%s previous-control=0x%08x,0x%08x,0x%08x",
                       reason ? reason : "startup", controls[0], controls[1], controls[2]);
}

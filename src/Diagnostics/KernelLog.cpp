#include "Diagnostics/KernelLog.hpp"
#include <IOKit/IOLib.h>
#include <libkern/libkern.h>
#include <Headers/kern_nvram.hpp>
#include <kern/thread.h>

namespace {

constexpr const char *lastLogKey = NVRAM_PREFIX(NVRAM_APPLE_BOOT_GUID, "AMDRadeonRembrandtHookLastLog");
constexpr size_t lastLogCapacity = 512;
IOSimpleLock *lastLogLock;
char lastLog[lastLogCapacity];
uint64_t lastLogSequence;

const char *getCategoryName(KernelLog::Category category) {
    switch (category) {
        case KernelLog::Category::Core: return "Core";
        case KernelLog::Category::Hook: return "Hook";
        case KernelLog::Category::Hardware: return "Hardware";
    }
    return "Core";
}

void saveLastLog(void *, wait_result_t) {
    uint64_t writtenSequence = 0;
    bool pendingSync = false;
    bool reportedFailure = false;
    for (;;) {
        IOSleep(1000);
        char snapshot[lastLogCapacity];
        auto interruptState = IOSimpleLockLockDisableInterrupt(lastLogLock);
        uint64_t sequence = lastLogSequence;
        memcpy(snapshot, lastLog, sizeof(snapshot));
        IOSimpleLockUnlockEnableInterrupt(lastLogLock, interruptState);
        if (sequence == writtenSequence && !pendingSync) continue;
        NVStorage storage;
        if (!storage.init()) {
            storage.deinit();
            if (!reportedFailure) IOLog("AMDRadeonRembrandtHook [WARN] [Core] [KernelLog] save: NVRAM unavailable\n");
            reportedFailure = true;
            continue;
        }
        if (sequence != writtenSequence) {
            if (storage.write(lastLogKey, reinterpret_cast<const uint8_t *>(snapshot),
                              static_cast<uint32_t>(strlen(snapshot)), NVStorage::OptRaw)) {
                writtenSequence = sequence;
                pendingSync = true;
                reportedFailure = false;
            } else {
                if (!reportedFailure) IOLog("AMDRadeonRembrandtHook [WARN] [Core] [KernelLog] save: NVRAM write failed\n");
                reportedFailure = true;
            }
        }
        if (pendingSync && storage.sync()) pendingSync = false;
        storage.deinit();
    }
}

}

void KernelLog::writeLog(bool persist, Category category, const char *level, const char *component, const char *operation,
                         const char *format, va_list arguments) {
    char message[512] {};
    if (format) vsnprintf(message, sizeof(message), format, arguments);
    if (persist && lastLogLock) {
        char entry[lastLogCapacity] {};
        snprintf(entry, sizeof(entry), "[%s] [%s] [%s] %s: %s", level, getCategoryName(category),
                 component ? component : "Unknown", operation ? operation : "unknown", message);
        auto interruptState = IOSimpleLockLockDisableInterrupt(lastLogLock);
        memcpy(lastLog, entry, sizeof(lastLog));
        ++lastLogSequence;
        IOSimpleLockUnlockEnableInterrupt(lastLogLock, interruptState);
    }
    IOLog("AMDRadeonRembrandtHook [%s] [%s] [%s] %s: %s\n", level, getCategoryName(category),
          component ? component : "Unknown", operation ? operation : "unknown", message);
}

void KernelLog::logInfo(Category category, const char *component, const char *operation, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    writeLog(true, category, "INFO", component, operation, format, arguments);
    va_end(arguments);
}

void KernelLog::logWarning(Category category, const char *component, const char *operation, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    writeLog(true, category, "WARN", component, operation, format, arguments);
    va_end(arguments);
}

void KernelLog::logError(Category category, const char *component, const char *operation, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    writeLog(true, category, "ERROR", component, operation, format, arguments);
    va_end(arguments);
}

void KernelLog::logDiagnosticInfo(Category category, const char *component, const char *operation, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    writeLog(false, category, "INFO", component, operation, format, arguments);
    va_end(arguments);
}

void KernelLog::logDiagnosticWarning(Category category, const char *component, const char *operation, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    writeLog(false, category, "WARN", component, operation, format, arguments);
    va_end(arguments);
}

bool KernelLog::startNvramLogging() {
    if (lastLogLock) return true;
    lastLogLock = IOSimpleLockAlloc();
    if (!lastLogLock) return false;
    thread_t thread;
    if (kernel_thread_start(saveLastLog, nullptr, &thread) != KERN_SUCCESS) {
        IOSimpleLockFree(lastLogLock);
        lastLogLock = nullptr;
        return false;
    }
    thread_deallocate(thread);
    return true;
}

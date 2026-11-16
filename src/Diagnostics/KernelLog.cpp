#include "Diagnostics/KernelLog.hpp"
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOKitKeys.h>
#include <libkern/c++/OSString.h>
#include <libkern/libkern.h>
#include <Headers/kern_nvram.hpp>
#include <Headers/kern_api.hpp>
#include <kern/thread.h>
#include <sys/fcntl.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/vnode_if.h>

namespace {

constexpr const char *lastLogKey = NVRAM_PREFIX(NVRAM_APPLE_BOOT_GUID, "AMDRadeonRembrandtHookLastLog");
constexpr size_t lastLogCapacity = 512;
IOSimpleLock *lastLogLock;
char lastLog[lastLogCapacity];
uint64_t lastLogSequence;

constexpr const char *diskLogPath = "/private/var/log/AMDRadeonRembrandtHook-kernel.log";
constexpr off_t diskLogLimit = 128LL * 1024 * 1024;
constexpr size_t diskLogChunk = 8192;
struct MessageBuffer {
    int msg_magic, msg_size, msg_bufx, msg_bufr;
    char *msg_bufc;
};
constexpr int messageBufferMagic = 0x063061;
MessageBuffer **messageBuffer;
void (*lockMessages)();
void (*unlockMessages)();
volatile int *systemInShutdown;

bool writeDiskLog(vnode_t vnode, vfs_context_t context, off_t &offset, const char *data, size_t length) {
    if (offset < 0 || offset + static_cast<off_t>(length) > diskLogLimit) return false;
    int remaining = 0;
    int error = vn_rdwr(UIO_WRITE, vnode, const_cast<char *>(data), static_cast<int>(length), offset,
                        UIO_SYSSPACE, IO_UNIT | IO_NOAUTH, vfs_context_ucred(context), &remaining,
                        vfs_context_proc(context));
    if (error || remaining) return false;
    offset += static_cast<off_t>(length);
    return true;
}

void saveKernelLog(void *, wait_result_t) {
    IOSleep(5000);
    auto context = vfs_context_create(nullptr);
    auto snapshot = static_cast<char *>(IOMalloc(diskLogChunk));
    if (!context || !snapshot) {
        if (snapshot) IOFree(snapshot, diskLogChunk);
        if (context) vfs_context_rele(context);
        return;
    }
    int cursor = 0;
    char *bufferIdentity = nullptr;
    int bufferSize = 0;
    size_t initialRemaining = 0;
    bool started = false;
    for (;;) {
        if (systemInShutdown && *systemInShutdown) break;
        size_t captured = 0;
        vnode_t vnode = nullptr;
        if (vnode_open(diskLogPath, O_CREAT | FWRITE, 0600, 0, &vnode, context)) {
            IOSleep(1000);
            continue;
        }
        vnode_attr attributes;
        VATTR_INIT(&attributes);
        VATTR_WANTED(&attributes, va_data_size);
        bool usable = vnode_getattr(vnode, &attributes, context) == 0;
        off_t offset = usable ? attributes.va_data_size : diskLogLimit;
        if (!started && usable && offset > diskLogLimit - static_cast<off_t>(diskLogChunk) - 128) {
            VATTR_INIT(&attributes);
            VATTR_SET(&attributes, va_data_size, 0);
            usable = vnode_setattr(vnode, &attributes, context) == 0;
            if (usable) offset = 0;
        }
        if (!started && usable) {
            static const char separator[] = "\n======== AMDRadeonRembrandtHook: new boot ========\n";
            usable = writeDiskLog(vnode, context, offset, separator, sizeof(separator) - 1);
            if (usable) started = true;
        }
        if (usable) {
            lockMessages();
            auto buffer = *messageBuffer;
            size_t length = 0;
            int next = cursor;
            if (buffer && buffer->msg_magic == messageBufferMagic && buffer->msg_bufc && buffer->msg_size > 0 &&
                buffer->msg_bufx >= 0 && buffer->msg_bufx < buffer->msg_size) {
                if (bufferIdentity != buffer->msg_bufc || bufferSize != buffer->msg_size) {
                    bufferIdentity = buffer->msg_bufc;
                    bufferSize = buffer->msg_size;
                    bool wrapped = buffer->msg_bufc[buffer->msg_bufx] != 0;
                    cursor = wrapped ? buffer->msg_bufx : 0;
                    initialRemaining = wrapped ? static_cast<size_t>(bufferSize) : static_cast<size_t>(buffer->msg_bufx);
                }
                int available = buffer->msg_bufx - cursor;
                if (available < 0) available += bufferSize;
                length = initialRemaining ? initialRemaining : static_cast<size_t>(available);
                if (length > diskLogChunk) length = diskLogChunk;
                size_t first = length;
                if (first > static_cast<size_t>(bufferSize - cursor)) first = bufferSize - cursor;
                memcpy(snapshot, buffer->msg_bufc + cursor, first);
                if (length > first) memcpy(snapshot + first, buffer->msg_bufc, length - first);
                next = (cursor + static_cast<int>(length)) % bufferSize;
            }
            unlockMessages();
            if (length && writeDiskLog(vnode, context, offset, snapshot, length)) {
                cursor = next;
                if (initialRemaining) initialRemaining -= length;
                captured = length;
                VNOP_FSYNC(vnode, MNT_WAIT, context);
                VNOP_IOCTL(vnode, F_FULLFSYNC, nullptr, 0, context);
            }
        }
        vnode_close(vnode, FWRITE, context);
        if (offset >= diskLogLimit - static_cast<off_t>(diskLogChunk)) break;
        IOSleep(captured == diskLogChunk ? 10 : 500);
    }
    IOFree(snapshot, diskLogChunk);
    vfs_context_rele(context);
}

void prepareDiskLogging(void *, KernelPatcher &patcher) {
    messageBuffer = reinterpret_cast<MessageBuffer **>(patcher.solveSymbol(KernelPatcher::KernelID, "_msgbufp"));
    patcher.clearError();
    lockMessages = reinterpret_cast<void (*)()>(patcher.solveSymbol(KernelPatcher::KernelID, "_bsd_log_lock_safe"));
    patcher.clearError();
    unlockMessages = reinterpret_cast<void (*)()>(patcher.solveSymbol(KernelPatcher::KernelID, "_bsd_log_unlock"));
    patcher.clearError();
    systemInShutdown = reinterpret_cast<volatile int *>(patcher.solveSymbol(KernelPatcher::KernelID, "_system_inshutdown"));
    patcher.clearError();
    if (!messageBuffer || !lockMessages || !unlockMessages || !systemInShutdown) {
        IOLog("AMDRadeonRembrandtHook [WARN] [Core] [KernelLog] disk: required kernel symbols unavailable\n");
        return;
    }
    thread_t thread;
    if (kernel_thread_start(saveKernelLog, nullptr, &thread) != KERN_SUCCESS) {
        IOLog("AMDRadeonRembrandtHook [WARN] [Core] [KernelLog] disk: unable to start worker\n");
        return;
    }
    thread_deallocate(thread);
    IOLog("AMDRadeonRembrandtHook [INFO] [Core] [KernelLog] disk: worker started\n");
}

bool requestNvramSync() {
    auto options = IORegistryEntry::fromPath("/options", gIODTPlane);
    if (!options) return false;
    auto value = OSString::withCString("now");
    bool accepted = value && options->setProperty(kIONVRAMSyncNowPropertyKey, value);
    if (value) value->release();
    options->release();
    return accepted;
}

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
        if (pendingSync && requestNvramSync()) pendingSync = false;
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

void KernelLog::startDiskLogging() {
    if (!checkKernelArgument("-rmbnodisklog")) lilu.onPatcherLoadForce(prepareDiskLogging);
}

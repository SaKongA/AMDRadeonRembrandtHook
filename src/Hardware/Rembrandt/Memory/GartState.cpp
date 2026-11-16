#include <IOKit/IOLib.h>
#include "Diagnostics/KernelLog.hpp"
#include "Diagnostics/Diagnostics.hpp"

extern bool mmioAvailable();
extern uint32_t mmioRead(uint32_t reg);
extern void mmioWrite(uint32_t reg, uint32_t value);
extern uint64_t vramRead64(uint64_t offset);
extern bool vramWrite32(uint64_t offset, uint32_t value);

namespace {

constexpr uint32_t gcBase = 0x1260;
uint64_t *savedEntries;
uint64_t savedTableOffset, savedEntryCount;

uint64_t readRegisterPair(uint32_t offset) {
    return static_cast<uint64_t>(mmioRead(gcBase + offset + 1)) << 32 | mmioRead(gcBase + offset);
}

bool getGartGeometry(uint64_t &tableOffset, uint64_t &entryCount) {
    uint64_t physicalOffset = static_cast<uint64_t>(mmioRead(gcBase + 0x16e7) & 0xffffff) << 24;
    uint64_t framebufferBase = static_cast<uint64_t>(mmioRead(gcBase + 0x16fc) & 0xffffff) << 24;
    uint64_t framebufferTop = (static_cast<uint64_t>(mmioRead(gcBase + 0x16fd) & 0xffffff) << 24) | 0xffffff;
    uint64_t base = readRegisterPair(0x1667) & 0x0000fffffffff000ull;
    uint64_t first = readRegisterPair(0x1687), last = readRegisterPair(0x16a7);
    tableOffset = base >= framebufferBase && base <= framebufferTop ? base - framebufferBase :
        base >= physicalOffset && physicalOffset ? base - physicalOffset : base;
    entryCount = last >= first ? last - first + 1 : 0;
    if (entryCount > 0x10000) entryCount = 0x10000;
    return entryCount != 0;
}

}

void gartSave() {
    if (!mmioAvailable()) {
        KernelLog::logWarning(KernelLog::Category::Hardware, "GART", "save", "GPU register aperture unavailable");
        return;
    }
    uint64_t table = 0, count = 0;
    if (!getGartGeometry(table, count)) {
        logGpuVmDiagnostics("save", table, count, false);
        KernelLog::logWarning(KernelLog::Category::Hardware, "GART", "save", "Invalid table geometry");
        return;
    }
    logGpuVmDiagnostics("save", table, count, true);
    if (!savedEntries) savedEntries = static_cast<uint64_t *>(IOMalloc(0x10000 * sizeof(uint64_t)));
    if (!savedEntries) {
        KernelLog::logError(KernelLog::Category::Hardware, "GART", "save", "Unable to allocate saved table");
        return;
    }
    uint64_t zeroCount = 0, allOnesCount = 0;
    for (uint64_t i = 0; i < count; ++i) {
        savedEntries[i] = vramRead64(table + i * 8);
        if (!savedEntries[i]) ++zeroCount;
        if (savedEntries[i] == ~0ull) ++allOnesCount;
    }
    savedTableOffset = table;
    savedEntryCount = count;
    logVramSaveDiagnostics(count, zeroCount, allOnesCount);
    KernelLog::logInfo(KernelLog::Category::Hardware, "GART", "save", "Saved table offset=0x%llx entries=%llu", static_cast<unsigned long long>(table), static_cast<unsigned long long>(count));
}

void gartRestore(const char *) {
    if (!savedEntries || !savedEntryCount || !mmioAvailable()) return;
    uint64_t table = 0, count = 0;
    if (!getGartGeometry(table, count) || table != savedTableOffset || count != savedEntryCount) {
        logGpuVmDiagnostics("restore", table, count, false);
        KernelLog::logWarning(KernelLog::Category::Hardware, "GART", "restore", "Table geometry changed; saved entries discarded");
        savedEntryCount = 0;
        return;
    }
    logGpuVmDiagnostics("restore", table, count, true);
    uint64_t checkedCount = 0, emptyCount = 0, mismatchCount = 0, allOnesCount = 0, writeFailureCount = 0;
    for (uint64_t i = 0; i < count; ++i) {
        uint64_t entry = savedEntries[i];
        if (entry && entry != ~0ull) {
            uint64_t current = vramRead64(table + i * 8);
            ++checkedCount;
            if (current == ~0ull) ++allOnesCount;
            else if (current && current != entry) ++mismatchCount;
            if (!current) {
                ++emptyCount;
                if (!vramWrite32(table + i * 8, static_cast<uint32_t>(entry))) ++writeFailureCount;
                if (!vramWrite32(table + i * 8 + 4, static_cast<uint32_t>(entry >> 32))) ++writeFailureCount;
            }
        }
    }
    logVramRestoreDiagnostics(checkedCount, emptyCount, mismatchCount, allOnesCount, writeFailureCount);
    savedEntryCount = 0;
    mmioWrite(gcBase + 0x1630, 1u | 0x00f80000u);
    for (uint32_t i = 0; i < 1000 && !(mmioRead(gcBase + 0x1642) & 1); ++i) IODelay(10);
    KernelLog::logInfo(KernelLog::Category::Hardware, "GART", "restore", "Restored table entries=%llu", static_cast<unsigned long long>(count));
}

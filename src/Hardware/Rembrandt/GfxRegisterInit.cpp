#include <IOKit/IOLib.h>
#include "Diagnostics/KernelLog.hpp"
#include <kern/thread.h>

extern uint32_t mmioRead(uint32_t reg);
extern void mmioWrite(uint32_t reg, uint32_t value);

namespace {

struct RegisterSetting { uint32_t reg, mask, value; };
constexpr uint32_t gcBase = 0x1260, gcExtendedBase = 0xa000;
constexpr RegisterSetting settings[] {
    {gcExtendedBase + 0x507c, 0xff7f0fff, 0x78000100},
    {gcExtendedBase + 0x2d90, 0x000000ff, 0x000000e4},
    {gcBase + 0x1f53, 0x0007ffff, 0x0000c200},
    {gcBase + 0x13ae, 0xffffffff, 0x00000280},
    {gcBase + 0x13af, 0xffffffff, 0x00800000},
    {gcBase + 0x13de, 0x0c1807ff, 0x00000242},
    {gcBase + 0x1580, 0x1ff1ffff, 0x00000500},
    {gcExtendedBase + 0x2d10, 0x000000ff, 0x000000e4},
    {gcExtendedBase + 0x2e25, 0x77777777, 0x32103210},
    {gcExtendedBase + 0x2e26, 0x77777777, 0x32103210},
    {gcExtendedBase + 0x2e21, 0xffffffff, 0xfffffff3},
    {gcExtendedBase + 0x2e03, 0xffffffff, 0xfffffff3},
    {gcExtendedBase + 0x2e08, 0xff8fff0f, 0x580f1008},
    {gcBase + 0x10a2, 0x000001ff, 0x00000020},
    {gcBase + 0x1025, 0xf17fffff, 0x01200007},
    {gcBase + 0x1070, 0xffffffff, 0x00000800},
    {gcBase + 0x107c, 0xffffffbf, 0x00000820},
    {gcBase + 0x1588, 0xffffffff, 0x00100000},
};

void watchGraphicsRegisterSettings(void *, wait_result_t);

}

void syncSdmaAddrConfig(const char *) {
    uint32_t addressConfig = mmioRead(gcBase + 0x13de);
    if (mmioRead(gcBase + 0x1e) == addressConfig && mmioRead(gcBase + 0x1f) == addressConfig) return;
    mmioWrite(gcBase + 0x1e, addressConfig);
    mmioWrite(gcBase + 0x1f, addressConfig);
}

void applyGcGolden() {
    for (const auto &setting : settings) {
        uint32_t old = mmioRead(setting.reg);
        uint32_t value = setting.mask == 0xffffffff ? setting.value :
            (old & ~setting.mask) | (setting.value & setting.mask);
        if (value != old) mmioWrite(setting.reg, value);
    }
    syncSdmaAddrConfig(nullptr);
}

namespace {

void watchGraphicsRegisterSettings(void *, wait_result_t) {
    for (uint32_t i = 0; i < 3000; ++i) {
        IOSleep(1);
        if (mmioRead(gcExtendedBase + 0x2e03) != 0xfffffff3) applyGcGolden();
    }
}

}

void startGoldenGuard() {
    thread_t thread;
    if (kernel_thread_start(watchGraphicsRegisterSettings, nullptr, &thread) == KERN_SUCCESS) {
        KernelLog::logInfo(KernelLog::Category::Hardware, "Graphics", "start guard", "Register settings guard started");
        thread_deallocate(thread);
    } else KernelLog::logError(KernelLog::Category::Hardware, "Graphics", "start guard", "Unable to start register settings guard");
}

#include "Kexts/AMDRadeonX6000/AcceleratorHooks.hpp"
#include <IOKit/IOLib.h>

extern uint32_t mmioRead(uint32_t reg);
extern void mmioWrite(uint32_t reg, uint32_t value);
extern void gartRestore(const char *when);
extern void syncSdmaAddrConfig(const char *when);

volatile bool gKiqSleepSkip;
mach_vm_address_t originalSubmitKiqFrame;
mach_vm_address_t originalGetRtHardwareChannel;
mach_vm_address_t getHardwareChannelByEngine;
mach_vm_address_t originalResetSdma;
mach_vm_address_t originalResetHardware;

namespace {

constexpr uint32_t gcBase = 0x1260;

uint64_t readObject64(uint64_t object, uint32_t offset) {
    return *reinterpret_cast<uint64_t *>(object + offset);
}

uint32_t readGcRegister(uint32_t offset) {
    return mmioRead(gcBase + offset);
}

uint64_t callVirtualSlot(uint64_t object, uint32_t offset) {
    return reinterpret_cast<uint64_t (*)(uint64_t)>((*reinterpret_cast<uint64_t **>(object))[offset / 8])(object);
}

void setEngineStatus(uint64_t hardware, uint16_t bit, bool enabled) {
    auto object = readObject64(hardware, 0x18);
    auto function = reinterpret_cast<uint64_t (*)(uint64_t)>((*reinterpret_cast<uint64_t **>(object))[0x178 / 8]);
    auto status = reinterpret_cast<uint16_t *>(function(object) + 0x8);
    *status = enabled ? (*status | bit) : (*status & ~bit);
}

void resetCommandQueues() {
    constexpr uint32_t mecHalt = (1u << 30) | (1u << 28);
    constexpr uint32_t meHalt = (1u << 28) | (1u << 26) | (1u << 24);
    auto selection = readGcRegister(0x0dc2);
    mmioWrite(gcBase + 0x0f55, mecHalt);
    mmioWrite(gcBase + 0x0f56, readGcRegister(0x0f56) | meHalt);
    IODelay(100);
    for (uint32_t engine = 1; engine <= 2; ++engine) {
        for (uint32_t pipe = 0; pipe < 4; ++pipe) {
            for (uint32_t queue = 0; queue < 8; ++queue) {
                mmioWrite(gcBase + 0x0dc2, (queue << 8) | (engine << 2) | pipe);
                mmioWrite(gcBase + 0x1fb8, 0);
                mmioWrite(gcBase + 0x1fab, 0);
                mmioWrite(gcBase + 0x1fc1, 0);
                mmioWrite(gcBase + 0x1fb3, 0);
                mmioWrite(gcBase + 0x1fdf, 0);
                mmioWrite(gcBase + 0x1fe0, 0);
                mmioWrite(gcBase + 0x1fdc, 0);
            }
        }
    }
    for (uint32_t pipe = 0; pipe < 2; ++pipe) {
        mmioWrite(gcBase + 0x0dc2, pipe);
        mmioWrite(gcBase + 0x1e80, 0);
        mmioWrite(gcBase + 0x1e93, 0);
    }
    mmioWrite(gcBase + 0x0dc2, selection);
    mmioWrite(gcBase + 0x0f56, readGcRegister(0x0f56) & ~meHalt);
    mmioWrite(gcBase + 0x0f55, 0);
    IODelay(1000);
}

}

uint64_t wrapSubmitKiqFrame(uint64_t self, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
    if (gKiqSleepSkip) return 0;
    return reinterpret_cast<AcceleratorHookFunction>(originalSubmitKiqFrame)(self, b, c, d, e, f);
}

uint64_t wrapStartKiq(uint64_t self, uint64_t firstAddress, uint64_t secondAddress, uint64_t specification, uint64_t, uint64_t) {
    auto ring = readObject64(self, 0x30);
    struct Input { uint32_t r0, r4; uint64_t r8, r10, r18, r20, r28, r30; } input {};
    input.r0 = *reinterpret_cast<uint32_t *>(ring + 0x84) & ~3u;
    input.r4 = 0x80;
    input.r8 = readObject64(ring, 0x48);
    input.r10 = readObject64(ring, 0xb8);
    input.r18 = readObject64(ring, 0xd0);
    input.r20 = firstAddress;
    input.r28 = secondAddress;
    input.r30 = 0x200;
    uint32_t output[6] {};
    gKiqSleepSkip = false;
    gartRestore("before startKIQ");
    resetCommandQueues();
    mmioWrite(gcBase + 0x15c4, readGcRegister(0x15c4) | 1);
    IODelay(10);
    mmioWrite(gcBase + 0x15c4, readGcRegister(0x15c4) & ~1u);
    auto owner = readObject64(self, 0x20);
    auto service = callVirtualSlot(owner, 0x2c0);
    auto function = (*reinterpret_cast<uint64_t **>(service))[0x120 / 8];
    auto result = static_cast<uint32_t>(reinterpret_cast<uint64_t (*)(uint64_t, uint64_t, uint64_t, void *, void *)>(function)(service, 8, 0, &input, output));
    auto spec = reinterpret_cast<uint32_t *>(specification);
    if (result) return result;
    if (output[0] != spec[0] || output[1] != spec[1] || output[2] != spec[2]) result = 0xe00002bc;
    *reinterpret_cast<uint64_t *>(ring + 0xc0) = *reinterpret_cast<uint64_t *>(&output[4]);
    auto selection = readGcRegister(0x0dc2);
    mmioWrite(gcBase + 0x0dc2, (spec[2] << 8) | (spec[0] << 2) | spec[1]);
    mmioWrite(gcBase + 0x1fdc, 0);
    mmioWrite(gcBase + 0x0dc2, selection);
    return result;
}

uint64_t wrapPowerUpHardwareEngines(uint64_t hardware, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) {
    bool success = true;
    for (uint32_t i = 0; i < 11 && success; ++i) {
        auto engine = readObject64(hardware, 0x3b0 + i * 8);
        if (engine) success = callVirtualSlot(engine, 0x138) & 0xff;
    }
    setEngineStatus(hardware, 0x100, success);
    return success;
}

uint64_t wrapStartHardwareEngines(uint64_t hardware, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) {
    uint32_t count = 2 + 2 * *reinterpret_cast<uint8_t *>(hardware + 0xc2);
    bool success = true;
    syncSdmaAddrConfig("before engine start");
    for (uint32_t i = 0; i < count && success; ++i) {
        auto engine = readObject64(hardware, 0x3b8 + i * 8);
        success = engine && (callVirtualSlot(engine, 0x148) & 0xff);
        syncSdmaAddrConfig("after engine start");
        if (!success && i + 1 == 2) success = true;
    }
    setEngineStatus(hardware, 0x200, success);
    return success;
}

uint64_t wrapGetRtHardwareChannel(uint64_t hardware, uint64_t type, uint64_t priority, uint64_t index, uint64_t e, uint64_t f) {
    if ((type & 0xffffffff) != 2 || !getHardwareChannelByEngine)
        return reinterpret_cast<AcceleratorHookFunction>(originalGetRtHardwareChannel)(hardware, type, priority, index, e, f);
    auto capabilities = reinterpret_cast<uint8_t *>(callVirtualSlot(hardware, 0x158));
    uint32_t p = priority & 0xffffffff, selectedIndex = index & 0xffffffff, ring = 0;
    if (p == 1) ring = capabilities[0x8f];
    else if (p == 0) { selectedIndex = 1; ring = capabilities[0x8f]; }
    uint32_t engine = ((capabilities[0x92] * 2 + 1) & selectedIndex) + 1;
    if (engine != 1) engine = 1;
    return reinterpret_cast<AcceleratorHookFunction>(getHardwareChannelByEngine)(hardware, engine, ring, 0, 0, 0);
}

uint64_t wrapResetSdma(uint64_t self, uint64_t ring, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
    auto result = reinterpret_cast<AcceleratorHookFunction>(originalResetSdma)(self, ring, c, d, e, f);
    syncSdmaAddrConfig("after SDMA reset");
    return result;
}

uint64_t wrapResetHardware(uint64_t self, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
    auto result = reinterpret_cast<AcceleratorHookFunction>(originalResetHardware)(self, b, c, d, e, f);
    syncSdmaAddrConfig("after resetHardware");
    return result;
}

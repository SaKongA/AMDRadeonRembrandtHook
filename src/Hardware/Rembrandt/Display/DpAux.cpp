#include "Hardware/Rembrandt/Display/DpAux.hpp"
#include "Diagnostics/KernelLog.hpp"
#include "Diagnostics/Diagnostics.hpp"
#include "Hardware/Rembrandt/Display/PanelBacklight.hpp"
#include <IOKit/IOLib.h>
#include <kern/thread.h>
#include <mach/mach_time.h>

extern bool mmioAvailable();
extern uint32_t mmioRead(uint32_t reg);
extern void mmioWrite(uint32_t reg, uint32_t value);

namespace {

constexpr uint32_t segment = 0x34c0;
constexpr uint32_t auxControl = segment + 0x1f50;
constexpr uint32_t auxArbitration = auxControl + 2;
constexpr uint32_t auxStatus = auxControl + 4;
constexpr uint32_t auxData = auxControl + 6;
constexpr uint32_t linkControl = segment + 0x2108;
constexpr uint32_t linkConfig = segment + 0x210b;
constexpr uint32_t videoStream = segment + 0x210c;
constexpr uint32_t phyControl = segment + 0x2117;
constexpr uint32_t trainingPattern = segment + 0x2118;
constexpr uint32_t prbsControl = segment + 0x211d;
constexpr uint32_t otgControl = segment + 0x1b41;
constexpr uint32_t pwmPeriod = segment + 0x2f1b;

IOLock *auxLock;
uint64_t lastDalAccess;
bool linkWatchPaused;
volatile bool linkRetrainingRequested;

struct LinkDiagnostics {
    bool attempted;
    bool success;
    uint32_t bandwidth;
    uint32_t lanes;
    uint32_t failures;
};

uint64_t getTimeMicroseconds() {
    uint64_t nanoseconds;
    absolutetime_to_nanoseconds(mach_absolute_time(), &nanoseconds);
    return nanoseconds / 1000;
}

int transferPanelAux(uint8_t command, uint32_t address, uint8_t *data, uint32_t length) {
    constexpr uint32_t indexWrite = 0x80000000u;
    uint32_t control = auxControl + 1;
    uint32_t interrupt = auxControl + 3;
    bool write = command == 0x80;
    mmioWrite(interrupt, mmioRead(interrupt) | 2);
    for (uint32_t i = 0; i < 100 && (mmioRead(auxStatus) & 1); ++i) IODelay(10);
    mmioWrite(control, (mmioRead(control) & ~0x1f00f1u) | (4 + (write ? length : 0)) << 16);
    uint32_t base = mmioRead(auxData) & ~(indexWrite | 0x1f0000u | 0xff00u | 1u);
    mmioWrite(auxData, base | indexWrite | static_cast<uint32_t>(command | ((address >> 16) & 0xf)) << 8);
    uint8_t header[] {static_cast<uint8_t>(address >> 8), static_cast<uint8_t>(address), static_cast<uint8_t>(length - 1)};
    for (uint8_t byte : header) mmioWrite(auxData, base | static_cast<uint32_t>(byte) << 8);
    for (uint32_t i = 0; write && i < length; ++i) mmioWrite(auxData, base | static_cast<uint32_t>(data[i]) << 8);
    mmioWrite(control, mmioRead(control) | 1);
    uint32_t status = 0;
    for (uint32_t i = 0; i < 1000 && !((status = mmioRead(auxStatus)) & 1); ++i) IODelay(10);
    uint32_t count = (status >> 24) & 0x1f;
    if (!(status & 1) || (status & 0x200) || !count) return -1;
    mmioWrite(auxData, base);
    mmioWrite(auxData, base | indexWrite);
    mmioWrite(auxData, base | indexWrite | 1);
    uint32_t reply = (mmioRead(auxData) >> 8 & 0xff) >> 4;
    if (reply) return -2;
    --count;
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t byte = static_cast<uint8_t>(mmioRead(auxData) >> 8);
        if (!write && i < length) data[i] = byte;
    }
    return static_cast<int>(count);
}

int readPanelDpcd(uint32_t address, uint8_t *data, uint32_t length) {
    return transferPanelAux(0x90, address, data, length);
}

template <size_t N>
void writePanelDpcd(uint32_t address, const uint8_t (&data)[N]) {
    uint8_t buffer[N];
    memcpy(buffer, data, N);
    transferPanelAux(0x80, address, buffer, N);
}

bool arePanelLanesReady(const uint8_t *status, uint32_t lanes, uint8_t bits) {
    for (uint32_t i = 0; i < lanes; ++i)
        if (((status[i / 2] >> (4 * (i & 1))) & bits) != bits) return false;
    return bits != 7 || (status[2] & 1);
}

void setPanelTrainingPattern(uint32_t pattern) {
    mmioWrite(trainingPattern, pattern);
    mmioWrite(linkControl, mmioRead(linkControl) & ~0x10u);
    mmioWrite(phyControl, mmioRead(phyControl) & ~0x10000u);
    mmioWrite(prbsControl, mmioRead(prbsControl) & ~1u);
}

bool trainPanelLink(uint8_t bandwidth, uint8_t lanes, uint32_t laneCount, uint8_t *status) {
    const uint8_t power[] {1};
    const uint8_t config[] {bandwidth, lanes};
    const uint8_t coding[] {1};
    writePanelDpcd(0x600, power);
    IODelay(2000);
    writePanelDpcd(0x100, config);
    writePanelDpcd(0x108, coding);
    setPanelTrainingPattern(0);
    uint8_t levels[4] {};
    bool clockRecovered = false;
    bool equalized = false;
    for (uint32_t phase = 0; phase < 2; ++phase) {
        if (phase && !clockRecovered) break;
        if (phase) setPanelTrainingPattern(1);
        for (uint32_t attempt = 0; attempt < 5; ++attempt) {
            uint8_t pattern[] {static_cast<uint8_t>(phase ? 0x22 : 0x21), levels[0], levels[1], levels[2], levels[3]};
            writePanelDpcd(0x102, pattern);
            if (phase) IOSleep(20); else IODelay(1000);
            if (readPanelDpcd(0x202, status, 6) != 6) break;
            bool ready = arePanelLanesReady(status, laneCount, phase ? 7 : 1);
            uint32_t adjustment = static_cast<uint32_t>(status[4]) | static_cast<uint32_t>(status[5]) << 8;
            for (uint32_t i = 0; i < 4; ++i)
                levels[i] = static_cast<uint8_t>(((adjustment >> (4 * i)) & 3) | (((adjustment >> (4 * i + 2)) & 3) << 3));
            if (ready) {
                if (phase) equalized = true; else clockRecovered = true;
                break;
            }
        }
    }
    const uint8_t patternOff[] {0};
    writePanelDpcd(0x102, patternOff);
    mmioWrite(linkControl, mmioRead(linkControl) | 0x10u);
    return equalized;
}

void checkPanelLink(LinkDiagnostics &diagnostics) {
    static uint8_t goodBandwidth, goodLanes;
    static uint32_t failures;
    static uint64_t nextAttempt;
    uint32_t laneCount = (mmioRead(linkConfig) & 3) + 1;
    uint8_t status[6] {}, config[2] {}, power = 0;
    if (readPanelDpcd(0x202, status, 3) != 3) {
        if ((mmioRead(auxStatus) & 0x200) && !(mmioRead(pwmPeriod) & 0xffff)) revivePanelPowerSequence();
        return;
    }
    if (arePanelLanesReady(status, laneCount, 7)) {
        if (!goodBandwidth && readPanelDpcd(0x100, config, 2) == 2 && config[0] && (config[1] & 0x1f) == laneCount) {
            goodBandwidth = config[0];
            goodLanes = config[1];
        }
        failures = 0;
        return;
    }
    uint64_t now = getTimeMicroseconds();
    if (now < nextAttempt) return;
    readPanelDpcd(0x100, config, 2);
    readPanelDpcd(0x600, &power, 1);
    uint8_t bandwidth = goodBandwidth, lanes = goodLanes;
    if (!bandwidth) {
        uint8_t caps[2] {};
        readPanelDpcd(0x001, caps, 2);
        bandwidth = caps[0];
        lanes = static_cast<uint8_t>(laneCount | (caps[1] & 0x80));
    }
    bool success = bandwidth && trainPanelLink(bandwidth, lanes, laneCount, status);
    failures = success ? 0 : failures + 1;
    diagnostics = {true, success, bandwidth, laneCount, failures};
    nextAttempt = now + (failures >= 3 ? 30000000 : 0);
}

void watchPanelLink(void *, wait_result_t) {
    for (;;) {
        for (uint32_t i = 0; i < 20 && !linkRetrainingRequested; ++i) IOSleep(100);
        linkRetrainingRequested = false;
        if (linkWatchPaused || !mmioAvailable()) continue;
        if (!(mmioRead(otgControl) & 1) || !(mmioRead(videoStream) & 1)) continue;
        bool trained = mmioRead(linkControl) & 0x10;
        LinkDiagnostics diagnostics {};
        IOLockLock(auxLock);
        uint32_t arbitration = mmioRead(auxArbitration);
        if (getTimeMicroseconds() - lastDalAccess >= (trained ? 200000 : 1000000) &&
            ((arbitration >> 2) & 3) != 2 && (mmioRead(auxControl) & 1)) {
            mmioWrite(auxArbitration, arbitration | 0x10000u);
            if (((mmioRead(auxArbitration) >> 2) & 3) == 1) checkPanelLink(diagnostics);
            mmioWrite(auxArbitration, (mmioRead(auxArbitration) & ~0x10000u) | 0x20000u);
        }
        IOLockUnlock(auxLock);
        if (diagnostics.attempted)
            logDisplayLinkDiagnostics(diagnostics.bandwidth, diagnostics.lanes, diagnostics.success, diagnostics.failures);
    }
}

}

bool startPanelLinkWatch() {
    if (auxLock) return true;
    auxLock = IOLockAlloc();
    if (!auxLock) {
        KernelLog::logError(KernelLog::Category::Hardware, "PanelLink", "start", "Unable to allocate AUX lock");
        return false;
    }
    thread_t thread;
    if (kernel_thread_start(watchPanelLink, nullptr, &thread) != KERN_SUCCESS) {
        IOLockFree(auxLock);
        auxLock = nullptr;
        KernelLog::logError(KernelLog::Category::Hardware, "PanelLink", "start", "Unable to start link watch");
        return false;
    }
    thread_deallocate(thread);
    KernelLog::logInfo(KernelLog::Category::Hardware, "PanelLink", "start", "AUX link watch started");
    return true;
}

void setPanelLinkWatchPaused(bool paused) {
    linkWatchPaused = paused;
}

void requestPanelLinkRetraining() {
    linkRetrainingRequested = true;
}

void recordDalAuxAccess(uint32_t reg) {
    if (reg < auxControl || reg > auxControl + 0x1b || !auxLock) return;
    IOLockLock(auxLock);
    lastDalAccess = getTimeMicroseconds();
    IOLockUnlock(auxLock);
}

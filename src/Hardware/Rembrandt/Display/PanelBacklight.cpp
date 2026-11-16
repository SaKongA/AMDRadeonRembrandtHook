#include "Hardware/Rembrandt/Display/PanelBacklight.hpp"
#include "Hardware/Rembrandt/Display/DmubInbox.hpp"
#include "Hardware/Rembrandt/Display/DpAux.hpp"
#include <IOKit/IOLib.h>

extern uint32_t mmioRead(uint32_t reg);
extern void mmioWrite(uint32_t reg, uint32_t value);

namespace {

constexpr uint32_t segmentDword = 0x34c0;
constexpr uint32_t softResetDword = segmentDword + 0x289e;
constexpr uint32_t sequenceControlDword = segmentDword + 0x2f14;
constexpr uint32_t delay1Dword = sequenceControlDword + 2;
constexpr uint32_t delay2Dword = sequenceControlDword + 3;
constexpr uint32_t refDiv1Dword = sequenceControlDword + 4;
constexpr uint32_t pwmControlDword = segmentDword + 0x2f19;
constexpr uint32_t pwmControl2Dword = pwmControlDword + 1;
constexpr uint32_t pwmPeriodDword = pwmControlDword + 2;
constexpr uint32_t pwmLockDword = pwmControlDword + 3;
constexpr uint32_t refDiv2Dword = sequenceControlDword + 9;

struct PanelRegisters {
    uint32_t control, delay1, delay2, refDiv1, pwmControl, pwmControl2, period, lock, refDiv2;
};

struct BacklightRegisters {
    uint32_t control, period, refDiv1, refDiv2;
};

PanelRegisters savedPanel;
BacklightRegisters savedBacklight;

bool isPanelProgrammed(uint32_t period, uint32_t control) {
    return (period & 0xffff) > 1 && (control & 0x80000000);
}

void restorePanelPowerSequence() {
    mmioWrite(refDiv1Dword, savedPanel.refDiv1);
    mmioWrite(sequenceControlDword, savedPanel.control);
    mmioWrite(delay1Dword, savedPanel.delay1);
    mmioWrite(delay2Dword, savedPanel.delay2);
    mmioWrite(refDiv2Dword, savedPanel.refDiv2);
    mmioWrite(pwmPeriodDword, savedPanel.period);
    mmioWrite(pwmControl2Dword, savedPanel.pwmControl2);
    mmioWrite(pwmControlDword, savedPanel.pwmControl);
    mmioWrite(pwmLockDword, savedPanel.lock & ~0x101u);
}

bool restorePanelPowerAfterReset() {
    if (!savedPanel.period) return false;
    uint32_t reset = mmioRead(softResetDword);
    mmioWrite(softResetDword, reset | (1u << 16));
    IODelay(50);
    mmioWrite(softResetDword, reset & ~(1u << 16));
    uint32_t waitUs = 0;
    while (!(mmioRead(pwmPeriodDword) & 0xffff) && waitUs < 1000) {
        IODelay(10);
        waitUs += 10;
    }
    uint32_t control = 0, period = 0;
    if (mmioRead(pwmPeriodDword) & 0xffff) {
        for (uint32_t attempt = 0; attempt < 5; ++attempt) {
            if (attempt) IODelay(100);
            restorePanelPowerSequence();
            control = mmioRead(sequenceControlDword);
            period = mmioRead(pwmPeriodDword);
            if (control == savedPanel.control && (period & 0xffff) == (savedPanel.period & 0xffff)) break;
        }
    }
    bool restored = control == savedPanel.control && isPanelProgrammed(period, mmioRead(pwmControlDword));
    if (restored) requestPanelLinkRetraining();
    return restored;
}

void setDmubBacklight(uint32_t level) {
    uint32_t command[16] {}, reply[16];
    uint32_t bits = (savedBacklight.period >> 16) & 0x1f;
    if (!bits || bits > 16) bits = 16;
    uint32_t period = savedBacklight.period & ((1u << bits) - 1);
    uint32_t active = static_cast<uint32_t>((static_cast<uint64_t>(period) * level >> 16) << (16 - bits));
    if (active > 0xffff) active = 0xffff;
    uint32_t control = (savedBacklight.control & ~0xffffu) | active | 0x80000000;
    command[0] = 0x2000004a;
    command[3] = control;
    command[4] = savedBacklight.period;
    command[5] = savedBacklight.refDiv1;
    command[6] = 3;
    command[7] = savedBacklight.refDiv2;
    if (dmubInjectCmd(command, reply)) savedBacklight.control = control;
}

}

bool revivePanelPowerSequence() {
    return restorePanelPowerAfterReset();
}

void savePanelPowerSequence() {
    PanelRegisters current {mmioRead(sequenceControlDword), mmioRead(delay1Dword), mmioRead(delay2Dword),
        mmioRead(refDiv1Dword), mmioRead(pwmControlDword), mmioRead(pwmControl2Dword),
        mmioRead(pwmPeriodDword), mmioRead(pwmLockDword), mmioRead(refDiv2Dword)};
    if (!isPanelProgrammed(current.period, current.pwmControl)) return;
    if (savedPanel.period && !(current.control & 0x10)) current.control = savedPanel.control;
    savedPanel = current;
}

void setBacklightLevel(uint32_t level, bool allowFirmware) {
    if (level > 0x10000) level = 0x10000;
    if (level < 0x10000 / 50) level = 0x10000 / 50;
    uint32_t periodRegister = mmioRead(pwmPeriodDword);
    uint32_t bits = (periodRegister >> 16) & 0x1f;
    uint32_t control = mmioRead(pwmControlDword);
    if (!(periodRegister & 0xffff) && revivePanelPowerSequence()) {
        periodRegister = mmioRead(pwmPeriodDword);
        bits = (periodRegister >> 16) & 0x1f;
        control = mmioRead(pwmControlDword);
    }
    if ((periodRegister & 0xffff) && !isPanelProgrammed(periodRegister, control) && savedPanel.period) {
        restorePanelPowerSequence();
        periodRegister = mmioRead(pwmPeriodDword);
        bits = (periodRegister >> 16) & 0x1f;
        control = mmioRead(pwmControlDword);
    }
    if (!(periodRegister & 0xffff) || !(control & 0x80000000)) {
        if (allowFirmware && savedBacklight.period && !(periodRegister & 0xffff)) setDmubBacklight(level);
        return;
    }
    if (!bits || bits > 16) bits = 16;
    uint32_t period = periodRegister & ((1u << bits) - 1);
    uint32_t active = static_cast<uint32_t>((static_cast<uint64_t>(period) * level >> 16) << (16 - bits));
    if (active > 0xffff) active = 0xffff;
    mmioWrite(pwmLockDword, mmioRead(pwmLockDword) | 1);
    mmioWrite(pwmControlDword, (control & ~0xffffu) | active);
    mmioWrite(pwmLockDword, mmioRead(pwmLockDword) & ~1u);
    uint32_t count = 0;
    while ((mmioRead(pwmLockDword) & 0x100) && count++ < 1000) IODelay(10);
    savedBacklight = {mmioRead(pwmControlDword), periodRegister, mmioRead(refDiv1Dword), mmioRead(refDiv2Dword)};
    savePanelPowerSequence();
}

void setBacklight(uint32_t level) {
    setBacklightLevel(level, true);
}

void setBacklightFromDal(uint32_t level) {
    setBacklightLevel(level, false);
}

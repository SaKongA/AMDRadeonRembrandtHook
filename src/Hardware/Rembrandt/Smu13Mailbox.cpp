#include "Hardware/Rembrandt/Smu13Mailbox.hpp"
#include "Diagnostics/KernelLog.hpp"
#include <IOKit/IOLib.h>

extern uint32_t mmioRead(uint32_t reg);
extern void mmioWrite(uint32_t reg, uint32_t value);

namespace {

constexpr uint32_t messageRegister = 0x16000 + 0x282;
constexpr uint32_t argumentRegister = 0x16000 + 0x292;
constexpr uint32_t responseRegister = 0x16000 + 0x29a;

bool waitSmuResponse(uint32_t &response, uint32_t timeoutMs) {
    for (uint32_t i = 0; i < timeoutMs * 100; ++i) {
        response = mmioRead(responseRegister);
        if (response) return true;
        IODelay(10);
    }
    return false;
}

}

uint32_t sendSmuMessage(uint32_t message, uint32_t &argument, uint32_t timeoutMs) {
    uint32_t response;
    if (!waitSmuResponse(response, 1000)) return 0;
    mmioWrite(responseRegister, 0);
    mmioWrite(argumentRegister, argument);
    mmioWrite(messageRegister, message);
    if (!waitSmuResponse(response, timeoutMs)) return 0;
    argument = mmioRead(argumentRegister);
    return response;
}

void haltGraphicsEngines() {
    constexpr uint32_t gcBase = 0x1260;
    constexpr uint32_t graphicsStatus = gcBase + 0x0da4;
    constexpr uint32_t mecControl = gcBase + 0x0f55;
    constexpr uint32_t meControl = gcBase + 0x0f56;
    constexpr uint32_t sdmaControl = gcBase + 0x002a;
    mmioWrite(mecControl, (1u << 30) | (1u << 28));
    mmioWrite(meControl, mmioRead(meControl) | (1u << 28) | (1u << 26) | (1u << 24));
    mmioWrite(sdmaControl, mmioRead(sdmaControl) | 1);
    for (uint32_t i = 0; i < 200 && (mmioRead(graphicsStatus) & (1u << 31)); ++i) IODelay(1000);
}

void resetGraphicsEngines() {
    uint32_t argument = 2;
    if (!sendSmuMessage(0x11, argument, 5000)) KernelLog::logWarning(KernelLog::Category::Hardware, "SMU", "reset graphics", "Reset message timed out");
    IOSleep(100);
}

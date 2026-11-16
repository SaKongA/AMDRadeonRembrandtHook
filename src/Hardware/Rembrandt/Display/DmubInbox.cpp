#include "Hardware/Rembrandt/Display/DmubInbox.hpp"
#include <IOKit/IOLib.h>

extern bool mmioAvailable();
extern uint32_t mmioRead(uint32_t reg);
extern void mmioWrite(uint32_t reg, uint32_t value);
extern uint64_t vramRead64(uint64_t offset);
extern bool vramWrite32(uint64_t offset, uint32_t value);

IOLock *gDmubInboxLock;
uint32_t gDalWritePointer = ~0u;
uint32_t gDmubAhead;

namespace {

constexpr uint32_t segmentDword = 0x34c0;
constexpr uint32_t writePointerDword = segmentDword + 0x1d6;
constexpr uint32_t readPointerDword = segmentDword + 0x1d7;
constexpr uint32_t windowLowDword = segmentDword + 0x1bd;
constexpr uint32_t windowHighDword = segmentDword + 0x1be;
constexpr uint32_t ringBytes = 0x2000;
constexpr uint32_t commandBytes = 64;
uint64_t realignedCommands[128][8];

}

bool initializeDmubInbox() {
    if (!gDmubInboxLock) gDmubInboxLock = IOLockAlloc();
    return gDmubInboxLock != nullptr;
}

bool isDmubInboxHeld() {
    return gDalWritePointer != ~0u && gDalWritePointer != mmioRead(writePointerDword);
}

uint64_t getDmubInboxOffset() {
    uint64_t framebufferBase = static_cast<uint64_t>(mmioRead(0x1260 + 0x16fc) & 0xffffff) << 24;
    return (static_cast<uint64_t>(mmioRead(windowHighDword)) << 32 | mmioRead(windowLowDword)) - framebufferBase;
}

void resetDmubInbox() {
    gDmubAhead = 0;
}

bool dmubInjectCmd(const uint32_t *command, uint32_t *reply) {
    if (!gDmubInboxLock || !mmioAvailable()) return false;
    IOLockLock(gDmubInboxLock);
    uint64_t ring = getDmubInboxOffset();
    uint32_t write = mmioRead(writePointerDword);
    uint32_t next = (write + commandBytes) % ringBytes;
    bool held = !gDmubAhead && isDmubInboxHeld();
    for (uint32_t i = 0; i < 16; ++i) vramWrite32(ring + write + i * 4, command[i]);
    if (!held) {
        if (gDalWritePointer == ~0u) gDalWritePointer = write;
        ++gDmubAhead;
    }
    mmioWrite(writePointerDword, next);
    uint32_t milliseconds = 0;
    while (mmioRead(readPointerDword) != next && milliseconds < 100) {
        IODelay(1000);
        ++milliseconds;
    }
    bool success = mmioRead(readPointerDword) == next;
    if (reply) {
        for (uint32_t i = 0; i < 16; i += 2) {
            uint64_t value = vramRead64(ring + write + i * 4);
            reply[i] = value & 0xffffffff;
            reply[i + 1] = value >> 32;
        }
    }
    IOLockUnlock(gDmubInboxLock);
    return success;
}

uint32_t realignDmubInbox(uint32_t writePointer) {
    uint64_t ring = getDmubInboxOffset();
    uint32_t hardwareWrite = mmioRead(writePointerDword), count = 0;
    for (uint32_t position = gDalWritePointer; position != writePointer && count < 128;
         position = (position + commandBytes) % ringBytes) {
        if (!(vramRead64(ring + position) & 0xff)) continue;
        for (uint32_t i = 0; i < 8; ++i) realignedCommands[count][i] = vramRead64(ring + position + i * 8);
        ++count;
    }
    uint32_t next = hardwareWrite;
    for (uint32_t i = 0; i < count; ++i, next = (next + commandBytes) % ringBytes) {
        for (uint32_t j = 0; j < 16; ++j)
            vramWrite32(ring + next + j * 4, (realignedCommands[i][j / 2] >> (j % 2 * 32)) & 0xffffffff);
    }
    uint32_t ahead = (next - writePointer + ringBytes) % ringBytes;
    if (ahead < 0x1000) gDmubAhead = ahead / commandBytes;
    else if (!count) gDmubAhead = 0;
    else {
        for (; next != writePointer; next = (next + commandBytes) % ringBytes) {
            for (uint32_t i = 0; i < 16; ++i) vramWrite32(ring + next + i * 4, 0);
        }
        gDmubAhead = 0;
    }
    return count ? next : ~0u;
}

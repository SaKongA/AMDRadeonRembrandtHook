#include "Hardware/Rembrandt/Display/UsbCDpLink.hpp"
#include "Hardware/Rembrandt/Display/DmubInbox.hpp"
#include <IOKit/IOLib.h>

extern uint32_t mmioRead(uint32_t reg);
extern void mmioWrite(uint32_t reg, uint32_t value);
extern uint64_t vramRead64(uint64_t offset);

namespace {
constexpr uint32_t segment = 0x34c0, auxFirst = segment + 0x1f6c, auxLast = segment + 0x1fbf, auxStride = 0x1c;
constexpr uint32_t indexWrite = 0x80000000u, indexMask = 0x1f0000u, dataMask = 0xff00u;
struct AuxRequest { uint8_t current[20], request[20]; uint32_t position, currentLength, requestLength, replyLength; };
struct Retimer { uint8_t lanes = 4, settings[4]; bool dpmf, intercept, eq3, training, eqPre; };
AuxRequest requests[3];
Retimer retimers[3];

int transferAux(uint32_t control, uint8_t command, uint32_t address, uint8_t *data, uint32_t length) {
    uint32_t swControl = control + 1, interrupt = control + 3, status = control + 4, swData = control + 6;
    bool write = command == 0x80;
    mmioWrite(interrupt, mmioRead(interrupt) | 2);
    for (int i = 0; i < 100 && (mmioRead(status) & 1); ++i) IODelay(10);
    mmioWrite(swControl, (mmioRead(swControl) & ~0x1f00f1u) | (4 + (write ? length : 0)) << 16);
    uint32_t base = mmioRead(swData) & ~(indexWrite | indexMask | dataMask | 1u);
    mmioWrite(swData, base | indexWrite | static_cast<uint32_t>(command | ((address >> 16) & 0xf)) << 8);
    uint8_t header[] {static_cast<uint8_t>(address >> 8), static_cast<uint8_t>(address), static_cast<uint8_t>(length - 1)};
    for (uint8_t byte : header) mmioWrite(swData, base | static_cast<uint32_t>(byte) << 8);
    for (uint32_t i = 0; write && i < length; ++i) mmioWrite(swData, base | static_cast<uint32_t>(data[i]) << 8);
    mmioWrite(swControl, mmioRead(swControl) | 1);
    uint32_t state = 0;
    for (int i = 0; i < 1000 && !((state = mmioRead(status)) & 1); ++i) IODelay(10);
    uint32_t count = (state >> 24) & 0x1f;
    if (!(state & 1) || (state & 0x200) || !count) return -1;
    mmioWrite(swData, base);
    mmioWrite(swData, base | indexWrite);
    mmioWrite(swData, base | indexWrite | 1);
    uint32_t reply = (mmioRead(swData) >> 8 & 0xff) >> 4;
    if (reply) return reply == 2 ? -3 : -2;
    --count;
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t byte = static_cast<uint8_t>(mmioRead(swData) >> 8);
        if (!write && i < length) data[i] = byte;
    }
    return static_cast<int>(count);
}

int transferRetimer(uint32_t control, uint8_t command, uint32_t address, uint8_t *data, uint32_t length) {
    int result = -3;
    for (int i = 0; i < 5 && (result = transferAux(control, command, address, data, length)) == -3; ++i) IODelay(400);
    return result;
}

bool writeRetimer(uint32_t control, uint8_t a, uint8_t b, uint8_t c) {
    uint8_t data[] {1, a, b, c};
    return transferRetimer(control, 0x80, 0xf004f, data, 4) >= 0;
}

void writeDpcd(uint32_t control, uint32_t address, uint8_t value) {
    transferRetimer(control, 0x80, address, &value, 1);
}

void configureFourLanes(uint32_t control) {
    writeRetimer(control, 0x6e, 0xf2, 0x19);
    writeRetimer(control, 0x6b, 0xf2, 0x01);
    writeRetimer(control, 0x6d, 0xf2, 0x18);
    writeRetimer(control, 0x6c, 0xf2, 0x03);
    writeRetimer(control, 0x03, 0xf3, 0x06);
}

void configureSignalLevels(uint32_t channel, uint32_t control) {
    auto &state = retimers[channel];
    uint8_t voltage = 0, emphasis = 0;
    for (uint32_t i = 0; i < state.lanes && i < 4; ++i) {
        voltage |= (state.settings[i] & 3) << (2 * i);
        emphasis |= ((state.settings[i] >> 3) & 3) << (2 * i);
    }
    writeRetimer(control, 0x51, 0x63, voltage);
    writeRetimer(control, 0x52, 0x63, emphasis);
}

void prepareRetimerRequest(uint32_t channel, const uint8_t *request, uint32_t length) {
    if (channel < 1 || channel > 2 || length < 4 || (request[0] >> 4) < 8) return;
    uint32_t control = auxFirst + channel * auxStride;
    auto &state = retimers[channel];
    bool changed = false;
    if (state.dpmf) { state.dpmf = false; changed = true; writeRetimer(control, 0x06, 0x70, 0x87); if (state.lanes == 4) configureFourLanes(control); }
    if (state.intercept) {
        state.intercept = false; changed = true;
        for (int i = 0; i < 10 && !writeRetimer(control, 0x55, 0x63, 0x6e); ++i) writeRetimer(control, 0x55, 0x63, 0x00);
    }
    if (state.eq3) { state.eq3 = false; changed = true; writeRetimer(control, 0x55, 0x63, 0x68); }
    if ((request[0] >> 4) == 8 && length > 4) {
        uint32_t address = (request[0] & 0xfu) << 16 | static_cast<uint32_t>(request[1]) << 8 | request[2];
        const uint8_t *data = request + 4;
        for (uint32_t i = 0; i < length - 4; ++i) {
            uint32_t current = address + i;
            if (current >= 0x103 && current <= 0x106) state.settings[current - 0x103] = data[i];
            if (current == 0x101) state.lanes = data[i] & 0x1f;
        }
        if (address == 0x107) {
            changed = state.training = true; state.eqPre = false;
            writeDpcd(control, 0x108, 1);
            writeRetimer(control, 0x50, 0x63, 0xff); writeRetimer(control, 0x51, 0x63, 0);
            writeRetimer(control, 0x52, 0x63, 0); writeRetimer(control, 0x55, 0x63, 0);
        } else if (address == 0x100) {
            changed = true; writeDpcd(control, 0x100, data[0] == 0x06 ? 0x0a : 0x06); state.dpmf = true;
        } else if (address == 0x102 && (data[0] & 0xf) == 1) state.intercept = true;
        else if (address == 0x102 && (data[0] & 0xf)) {
            changed = true;
            if (!state.eqPre) { writeRetimer(control, 0x55, 0x63, 0x2e); writeRetimer(control, 0x55, 0x63, 0x01); }
            state.eqPre = false; configureSignalLevels(channel, control); state.eq3 = true;
        } else if (address == 0x102) state.training = state.eqPre = false;
        else if (address >= 0x103 && address <= 0x106) { changed = true; configureSignalLevels(channel, control); }
    }
    if (!changed) return;
    uint32_t interrupt = control + 3, status = control + 4, swData = control + 6;
    mmioWrite(interrupt, mmioRead(interrupt) | 2);
    for (int i = 0; i < 100 && (mmioRead(status) & 1); ++i) IODelay(10);
    uint32_t base = mmioRead(swData) & ~(indexWrite | indexMask | dataMask | 1u);
    mmioWrite(swData, base | indexWrite | static_cast<uint32_t>(request[0]) << 8);
    for (uint32_t i = 1; i < length; ++i) mmioWrite(swData, base | static_cast<uint32_t>(request[i]) << 8);
}
}

uint64_t adjustUsbCDpWrite(uint32_t reg, uint64_t value) {
    if (reg == segment + 0x232b || reg == segment + 0x242b) value &= ~0x00800000ull;
    else if (reg == segment + 0x2287 || reg == segment + 0x2387) value |= 1;
    return value;
}

void prepareUsbCDpWrite(uint32_t reg, uint32_t value) {
    if ((reg == segment + 0x2318 || reg == segment + 0x2418) && value) {
        uint32_t channel = (reg - segment - 0x2118) / 0x100 - 1;
        if (retimers[channel].training) {
            uint32_t control = auxFirst + channel * auxStride;
            writeRetimer(control, 0x55, 0x63, 0x2e); writeRetimer(control, 0x55, 0x63, 0x01);
            retimers[channel].eqPre = true;
        }
    }
    if ((reg == segment + 0x2308 || reg == segment + 0x2408) && (value & 0x10)) {
        uint32_t channel = (reg - segment - 0x2108) / 0x100 - 1;
        if (retimers[channel].training) {
            uint32_t control = auxFirst + channel * auxStride;
            uint8_t zero = 0, status[3] {};
            transferRetimer(control, 0x80, 0x102, &zero, 1);
            transferRetimer(control, 0x90, 0x202, status, 3);
            IODelay(1000);
        }
    }
    if (reg < auxFirst || reg > auxLast) return;
    uint32_t channel = (reg - auxFirst) / auxStride, offset = (reg - auxFirst) % auxStride;
    auto &state = requests[channel];
    if (offset == 6 && !(value & 1)) {
        if (value & indexWrite) state.position = (value >> 16) & 0x1f;
        if (state.position < sizeof(state.current)) {
            state.current[state.position++] = static_cast<uint8_t>(value >> 8);
            if (state.position > state.currentLength) state.currentLength = state.position;
        }
        if (!state.position || ((value & indexWrite) && !((value >> 16) & 0x1f))) state.currentLength = state.position;
    } else if (offset == 1 && (value & 1)) {
        prepareRetimerRequest(channel, state.current, state.currentLength);
        memcpy(state.request, state.current, sizeof(state.request));
        state.requestLength = state.currentLength; state.replyLength = 0;
        state.currentLength = state.position = 0;
    }
}

uint32_t adjustUsbCDpRead(uint32_t reg, uint32_t value) {
    if (reg < auxFirst || reg > auxLast || (reg - auxFirst) % auxStride != 6) return value;
    uint32_t channel = (reg - auxFirst) / auxStride;
    auto &state = requests[channel];
    uint32_t index = state.replyLength;
    if (channel && state.requestLength >= 4 && (state.request[0] >> 4) == 9 && index >= 2) {
        uint32_t address = (state.request[0] & 0xfu) << 16 | static_cast<uint32_t>(state.request[1]) << 8 | state.request[2];
        uint32_t length = state.request[3] + 1u;
        constexpr uint32_t caps[] {0x00002, 0x02202};
        for (uint32_t cap : caps) {
            if (cap < address || cap - address >= length || index - 2 != cap - address) continue;
            uint32_t byte = (value >> 8) & 0xff;
            if ((byte & 0x1f) > 2) value = (value & ~0xff00u) | ((byte & 0xe0) | 2) << 8;
            break;
        }
    }
    if (state.replyLength < 20) ++state.replyLength;
    return value;
}

void prepareUsbCDpCommands(uint32_t writePointer) {
    uint32_t old = mmioRead(segment + 0x1d6);
    uint64_t ring = getDmubInboxOffset();
    for (uint32_t offset = old; offset != writePointer; offset = (offset + 64) % 0x2000) {
        uint64_t command = vramRead64(ring + offset);
        uint32_t data = command >> 32, phy = data & 0xff;
        if ((command & 0xffff) == 0x0180 && (phy == 2 || phy == 3) && ((data >> 8) & 0xff) == 1 && (data >> 24) == 4)
            configureFourLanes(auxFirst + (phy - 1) * auxStride);
    }
}

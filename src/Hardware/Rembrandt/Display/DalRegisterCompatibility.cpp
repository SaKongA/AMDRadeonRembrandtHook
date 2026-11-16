#include "Hardware/Rembrandt/Display/DalRegisterCompatibility.hpp"

extern uint32_t mmioRead(uint32_t reg);
extern void mmioWrite(uint32_t reg, uint32_t value);

namespace {

constexpr uint32_t segmentDword = 0x34c0;
constexpr uint32_t powerGatingFirst = segmentDword + 0x80;
constexpr uint32_t powerGatingLast = segmentDword + 0xaa;
constexpr uint32_t pipeStride = 0xdc;
constexpr uint32_t surfaceAddressFirst = 0x3aca;
constexpr uint32_t surfaceControlFirst = 0x3ada;
constexpr uint32_t apertureLowFirst = 0x3af5;
constexpr uint32_t apertureHighFirst = 0x3af6;
constexpr uint32_t framebufferBase = 0x3935;
constexpr uint32_t framebufferTop = 0x3936;
constexpr uint32_t compressedBufferControl = 0x398b;
constexpr uint32_t detileControlFirst = 0x398c;
constexpr uint32_t dramStateControl = 0x39bc;
constexpr uint32_t dtoModuloFirst = 0x142;

constexpr uint32_t phyFirst = segmentDword + 0x2928;
constexpr uint32_t phyLast = segmentDword + 0x2d8e;
constexpr uint32_t phyBFirst = segmentDword + 0x2a00;
constexpr uint32_t validPhyRegisters[] {
    0xff33ff00, 0x00777fff, 0, 0, 0, 0,
    0xff000000, 0xffffffff, 0x0003ffff, 0, 0, 0,
    0, 0xffff0000, 0xffffffff, 0x000003ff, 0, 0,
    0, 0, 0xffffff00, 0xffffffff, 0x00000003, 0,
    0, 0, 0, 0xffffffff, 0x03ffffff, 0,
    0, 0, 0, 0, 0x00000800, 0
};

uint32_t powerGatingShadow[powerGatingLast - powerGatingFirst + 1];
bool powerDomainsForced;

bool isPipeRegister(uint32_t reg, uint32_t first) {
    return reg >= first && reg <= first + 3 * pipeStride && (reg - first) % pipeStride == 0;
}

bool isPowerGatingStatus(uint32_t offset) {
    return (offset >= 0x81 && offset <= 0x93 && (offset & 1)) ||
        (offset >= 0xa2 && offset <= 0xaa && !(offset & 1));
}

bool isOdmSourceRegister(uint32_t reg) {
    return reg >= 0x4f8b && reg <= 0x4fbb && (reg & 0xf) == 0xb;
}

void forcePowerDomainsOn() {
    if (powerDomainsForced) return;
    powerDomainsForced = true;
    constexpr uint32_t domains[] {0x80, 0x82, 0x84, 0x86, 0x89, 0x8b, 0x8d};
    for (uint32_t domain : domains) {
        uint32_t value = mmioRead(segmentDword + domain);
        mmioWrite(segmentDword + domain, (value | 1u) & ~(1u << 8));
    }
}

void configureSystemAperture(uint32_t pipe) {
    uint32_t base = mmioRead(framebufferBase) & 0xffffff;
    uint32_t top = mmioRead(framebufferTop) & 0xffffff;
    if (!base || top < base) return;
    uint32_t low = base << 6;
    uint32_t high = ((top + 1) << 6) - 1;
    uint32_t offset = pipe * pipeStride;
    if (mmioRead(apertureLowFirst + offset) == low && mmioRead(apertureHighFirst + offset) == high) return;
    mmioWrite(apertureLowFirst + offset, low);
    mmioWrite(apertureHighFirst + offset, high);
}

void configureDetileBuffer(uint32_t pipe) {
    if (mmioRead(detileControlFirst + pipe) & 7) return;
    uint32_t used = mmioRead(compressedBufferControl) & 0x1f;
    for (uint32_t i = 0; i < 4; ++i) used += mmioRead(detileControlFirst + i) & 7;
    uint32_t segments = used >= 28 ? 0 : (28 - used < 6 ? 28 - used : 6);
    if (segments) mmioWrite(detileControlFirst + pipe, segments);
}

}

bool isDalPhyRegister(uint32_t reg) {
    return reg >= phyFirst && reg <= phyLast;
}

bool isUnavailableDalPhyRegister(uint32_t reg) {
    if (!isDalPhyRegister(reg)) return false;
    if (reg >= phyBFirst) return true;
    uint32_t index = reg - phyFirst;
    return !(validPhyRegisters[index / 32] & (1u << (index % 32)));
}

bool isDalPowerGatingRegister(uint32_t reg) {
    return reg >= powerGatingFirst && reg <= powerGatingLast;
}

uint32_t readDalPowerGatingRegister(uint32_t reg) {
    if (isPowerGatingStatus(reg - segmentDword))
        return (powerGatingShadow[reg - 1 - powerGatingFirst] & (1u << 8)) ? (2u << 30 | 2u << 28) : 0;
    return powerGatingShadow[reg - powerGatingFirst];
}

void writeDalPowerGatingRegister(uint32_t reg, uint32_t value) {
    powerGatingShadow[reg - powerGatingFirst] = value;
    forcePowerDomainsOn();
}

uint32_t translateDalRegisterRead(uint32_t reg, uint32_t value) {
    if (isOdmSourceRegister(reg)) return (value & 3) | ((value >> 8 & 3) << 2) | ((value >> 16) << 8);
    if (isPipeRegister(reg, 0x3b2c)) return (value & 0xffff0000) | ((value >> 15 & 1) << 12);
    return value;
}

uint32_t translateDalRegisterWrite(uint32_t reg, uint32_t value, uint32_t previous) {
    if (isOdmSourceRegister(reg)) return (value & 3) | ((value >> 2 & 3) << 8) | ((value >> 8 & 0xffff) << 16);
    if (isPipeRegister(reg, 0x3b2c)) return (value & 0xffff0000) | (previous & 0x1ff0) | ((value >> 12 & 1) << 15);
    return value;
}

uint32_t prepareDalRegisterWrite(uint32_t reg, uint32_t value) {
    if (reg >= dtoModuloFirst && reg <= dtoModuloFirst + 12 && (reg - dtoModuloFirst) % 4 == 0 && value)
        value = static_cast<uint32_t>(static_cast<uint64_t>(value) * 600000 / 730000);
    if (reg == dramStateControl) value = (value & ~1u) | 2u;
    if (isPipeRegister(reg, surfaceAddressFirst)) {
        uint32_t pipe = (reg - surfaceAddressFirst) / pipeStride;
        configureSystemAperture(pipe);
        configureDetileBuffer(pipe);
    }
    if (isPipeRegister(reg, surfaceControlFirst)) value &= ~0x2626u;
    return value;
}

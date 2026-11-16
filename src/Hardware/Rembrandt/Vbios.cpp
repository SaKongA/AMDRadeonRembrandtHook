#include "Hardware/Rembrandt/Vbios.hpp"
#include "Hardware/Rembrandt/PciIdentity.hpp"
#include "Diagnostics/KernelLog.hpp"
#include <IOKit/IOService.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/acpi/IOACPIPlatformExpert.h>
#include <libkern/c++/OSData.h>

namespace {

class AcpiPlatformAccess : public IOACPIPlatformExpert {
public:
    const OSData *getVfct() { return getACPITableData("VFCT", 0); }
};

uint16_t read16(const uint8_t *p) {
    return static_cast<uint16_t>(p[0]) | static_cast<uint16_t>(p[1]) << 8;
}

uint32_t read32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 |
        static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24;
}

bool validAtomRom(const uint8_t *rom, uint32_t size) {
    if (size < 0x4a || rom[0] != 0x55 || rom[1] != 0xaa) return false;
    uint32_t offset = read16(rom + 0x48);
    return offset && offset <= size && size - offset >= 8 &&
        (!memcmp(rom + offset + 4, "ATOM", 4) || !memcmp(rom + offset + 4, "MOTA", 4));
}

OSData *copyVbios(uint32_t deviceId) {
    auto platform = IOService::getPlatform();
    if (!platform || !platform->metaCast("IOACPIPlatformExpert")) return nullptr;
    auto table = static_cast<AcpiPlatformAccess *>(platform)->getVfct();
    if (!table || table->getLength() < 0x4c || table->getLength() > 512 * 1024) return nullptr;
    auto bytes = static_cast<const uint8_t *>(table->getBytesNoCopy());
    if (!bytes || memcmp(bytes, "VFCT", 4)) return nullptr;
    uint32_t length = read32(bytes + 4);
    if (length < 0x4c || length > table->getLength()) return nullptr;
    uint8_t checksum = 0;
    for (uint32_t i = 0; i < length; ++i) checksum += bytes[i];
    if (checksum) return nullptr;
    uint32_t offset = read32(bytes + 0x34);
    if (offset < 0x4c || offset >= length) return nullptr;
    while (offset < length) {
        if (length - offset < 28) return nullptr;
        auto vendor = read16(bytes + offset + 12);
        auto device = read16(bytes + offset + 14);
        auto size = read32(bytes + offset + 24);
        offset += 28;
        if (!size || size > length - offset) return nullptr;
        if (vendor == 0x1002 && device == deviceId) {
            if (!validAtomRom(bytes + offset, size)) return nullptr;
            return OSData::withBytes(bytes + offset, size);
        }
        offset += size;
    }
    return nullptr;
}

}

bool publishRembrandtVbios(IORegistryEntry *device, uint32_t deviceId) {
    if (!device || !isRembrandtDeviceId(deviceId)) return false;
    auto rom = copyVbios(deviceId);
    if (rom) {
        bool published = device->setProperty("ATY,bin_image", rom);
        KernelLog::logInfo(KernelLog::Category::Hardware, "VBIOS", "publish", "VFCT image size=%u published=%u", rom->getLength(), static_cast<unsigned int>(published));
        rom->release();
        return published;
    } else {
        KernelLog::logWarning(KernelLog::Category::Hardware, "VBIOS", "publish", "No valid VFCT image for device %04x", deviceId);
    }
    return false;
}

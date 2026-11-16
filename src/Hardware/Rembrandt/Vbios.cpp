#include <stddef.h>
#include <stdint.h>
#ifndef REMBRANDT_VBIOS_HOST_TEST
#include "Hardware/Rembrandt/Vbios.hpp"
#include "Hardware/Rembrandt/PciIdentity.hpp"
#include "Diagnostics/KernelLog.hpp"
#include <IOKit/IOService.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/acpi/IOACPIPlatformExpert.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSNumber.h>
#else
#include <cstring>
#endif

namespace {

uint16_t read16(const uint8_t *p) {
    return static_cast<uint16_t>(p[0]) | static_cast<uint16_t>(p[1]) << 8;
}

uint32_t read32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 |
        static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24;
}

void write16(uint8_t *p, uint16_t value) {
    p[0] = value;
    p[1] = value >> 8;
}

void write32(uint8_t *p, uint32_t value) {
    write16(p, value);
    write16(p + 2, value >> 16);
}

struct AtomLayout {
    uint32_t pci;
    uint32_t tables;
    uint32_t tableSize;
};

bool parseAtomLayout(const uint8_t *rom, uint32_t size, uint32_t deviceId, AtomLayout &layout) {
    if (!rom || size < 0x4a || size > 0x10000 || size % 512 || rom[0] != 0x55 || rom[1] != 0xaa ||
        static_cast<uint32_t>(rom[2]) * 512 != size) return false;
    uint32_t pci = read16(rom + 0x18);
    uint32_t header = read16(rom + 0x48);
    if (pci > size - 0x18 || header > size - 0x22 || memcmp(rom + pci, "PCIR", 4) ||
        read16(rom + pci + 4) != 0x1002 || read16(rom + pci + 6) != deviceId ||
        read16(rom + pci + 0x10) * 512U != size || !(rom[pci + 0x15] & 0x80) ||
        memcmp(rom + header + 4, "ATOM", 4) || read16(rom + header) < 0x22 ||
        read16(rom + header) > size - header || rom[header + 2] != 2 ||
        (rom[header + 3] != 2 && rom[header + 3] != 3)) return false;
    uint32_t tables = read16(rom + header + 0x20);
    if (tables > size - 0x3e) return false;
    uint32_t tableSize = read16(rom + tables);
    if (tableSize < 0x3e || tableSize > size - tables || rom[tables + 2] != 2 || rom[tables + 3] != 1) return false;
    uint8_t checksum = 0;
    for (uint32_t i = 0; i < size; ++i) checksum += rom[i];
    if (checksum) return false;
    const uint32_t slots[] {0x16, 0x3c};
    for (uint32_t slot : slots) {
        uint32_t offset = read16(rom + tables + slot);
        if (offset && (offset > size - 4 || read16(rom + offset) < 4 || read16(rom + offset) > size - offset)) return false;
    }
    layout = {pci, tables, tableSize};
    return true;
}

uint32_t readUmaWidth(const uint8_t *rom, uint32_t size, const AtomLayout &layout) {
    if (layout.tableSize < 0x42) return 0;
    uint32_t offset = read16(rom + layout.tables + 0x40);
    if (!offset || offset > size - 28 || read16(rom + offset) < 28 || read16(rom + offset) > size - offset ||
        rom[offset + 2] != 2 || (rom[offset + 3] != 1 && rom[offset + 3] != 2)) return 0;
    uint8_t type = rom[offset + 26];
    uint8_t channels = rom[offset + 27];
    if (!channels || channels > 8 || (type != 26 && type != 30 && type != 34 && type != 35)) return 0;
    return channels * (type == 35 ? 32U : 64U);
}

struct AugmentResult {
    uint32_t size;
    bool pspAdded;
    bool vramAdded;
    bool vramMissing;
};

AugmentResult augmentVbios(const uint8_t *source, uint32_t size, uint32_t deviceId, uint32_t vramMiB,
                           uint32_t width, uint8_t *output, uint32_t capacity) {
    AtomLayout layout {};
    if (!output || !parseAtomLayout(source, size, deviceId, layout)) return {};
    bool addPsp = read16(source + layout.tables + 0x16) == 0;
    bool missingVram = read16(source + layout.tables + 0x3c) == 0;
    bool validParams = vramMiB >= 16 && vramMiB <= 65536 && width >= 16 && width <= 256 && width % 16 == 0;
    bool addVram = missingVram && validParams;
    uint32_t end = size;
    uint32_t psp = 0, vram = 0;
    if (addPsp) {
        psp = (end + 15) & ~15U;
        end = psp + 0x414;
    }
    if (addVram) {
        vram = (end + 15) & ~15U;
        end = vram + 0x358;
    }
    if (end > 0x10000 || psp > 0xffff || vram > 0xffff) return {};
    uint32_t outputSize = (end + 511) & ~511U;
    if (outputSize > 0x10000 || outputSize > capacity) return {};
    for (uint32_t i = 0; i < outputSize; ++i) output[i] = i < size ? source[i] : 0;
    write16(output + layout.pci + 6, 0x73ff);
    if (addPsp) {
        write16(output + layout.tables + 0x16, psp);
        write16(output + psp, 0x414);
        output[psp + 2] = 2;
        output[psp + 3] = 1;
        output[psp + 4] = '$'; output[psp + 5] = 'P'; output[psp + 6] = 'S'; output[psp + 7] = 'P';
    }
    if (addVram) {
        write16(output + layout.tables + 0x3c, vram);
        write16(output + vram, 0x358);
        output[vram + 2] = 2;
        output[vram + 3] = 6;
        output[vram + 20] = 1;
        uint8_t *module = output + vram + 24;
        uint32_t channels = width / 16;
        write32(module, vramMiB);
        write32(module + 4, (1U << channels) - 1);
        write16(module + 20, 0x34);
        module[23] = 0x70;
        module[24] = channels;
        module[25] = 4;
        const char name[] = "RMB-UMA";
        for (size_t i = 0; i < sizeof(name); ++i) module[32 + i] = name[i];
    }
    output[2] = outputSize / 512;
    write16(output + layout.pci + 0x10, outputSize / 512);
    output[0x21] = 0;
    uint8_t checksum = 0;
    for (uint32_t i = 0; i < outputSize; ++i) checksum += output[i];
    output[0x21] = -checksum;
    return {outputSize, addPsp, addVram, missingVram && !addVram};
}

#ifndef REMBRANDT_VBIOS_HOST_TEST

class AcpiPlatformAccess : public IOACPIPlatformExpert {
public:
    const OSData *getVfct() { return getACPITableData("VFCT", 0); }
};

uint32_t registryUint32(IORegistryEntry *device, const char *name) {
    auto object = device->getProperty(name);
    if (auto number = OSDynamicCast(OSNumber, object)) return number->unsigned32BitValue();
    auto data = OSDynamicCast(OSData, object);
    if (!data || data->getLength() != 4) return 0;
    return read32(static_cast<const uint8_t *>(data->getBytesNoCopy()));
}

uint32_t readUmaSize(IOPCIDevice *device) {
    constexpr uint32_t offset = 0xde3 * 4;
    if (!device || !(device->configRead16(kIOPCIConfigCommand) & kIOPCICommandMemorySpace)) return 0;
    auto memory = device->getDeviceMemoryWithRegister(kIOPCIConfigBaseAddress5);
    if (!memory || memory->getLength() < offset + sizeof(uint32_t)) return 0;
    auto map = memory->map(kIOMapInhibitCache);
    if (!map) return 0;
    auto base = reinterpret_cast<volatile uint32_t *>(map->getVirtualAddress());
    uint32_t size = base ? base[offset / 4] : 0;
    map->release();
    return size >= 16 && size <= 65536 ? size : 0;
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

#endif
}

#ifndef REMBRANDT_VBIOS_HOST_TEST
bool publishRembrandtVbios(IORegistryEntry *device, uint32_t deviceId) {
    if (!device || !isRembrandtDeviceId(deviceId)) return false;
    auto rom = copyVbios(deviceId);
    if (rom) {
        auto bytes = static_cast<const uint8_t *>(rom->getBytesNoCopy());
        auto pci = OSDynamicCast(IOPCIDevice, device);
        AtomLayout layout {};
        bool valid = parseAtomLayout(bytes, rom->getLength(), deviceId, layout);
        uint32_t vramMiB = registryUint32(device, "rmb-vram-size-mb");
        uint32_t width = registryUint32(device, "rmb-memory-width");
        if (valid && !read16(bytes + layout.tables + 0x3c)) {
            if (!vramMiB) vramMiB = readUmaSize(pci);
            if (!width) width = readUmaWidth(bytes, rom->getLength(), layout);
        }
        constexpr uint32_t maxImageSize = 0x10000;
        auto augmented = valid ? static_cast<uint8_t *>(IOMalloc(maxImageSize)) : nullptr;
        if (augmented) {
            auto result = augmentVbios(bytes, rom->getLength(), deviceId, vramMiB, width, augmented, maxImageSize);
            if (result.size) {
                auto replacement = OSData::withBytes(augmented, result.size);
                if (replacement) {
                    rom->release();
                    rom = replacement;
                    KernelLog::logInfo(KernelLog::Category::Hardware, "VBIOS", "augment", "PSP added=%u VRAM added=%u size=%u MiB width=%u bits",
                                       static_cast<unsigned int>(result.pspAdded), static_cast<unsigned int>(result.vramAdded), vramMiB, width);
                    if (result.vramMissing)
                        KernelLog::logWarning(KernelLog::Category::Hardware, "VBIOS", "augment", "VRAM parameters unavailable; set rmb-vram-size-mb and rmb-memory-width");
                }
            } else KernelLog::logWarning(KernelLog::Category::Hardware, "VBIOS", "augment", "Image cannot be extended; using original VBIOS");
            IOFree(augmented, maxImageSize);
        } else if (!valid) KernelLog::logWarning(KernelLog::Category::Hardware, "VBIOS", "augment", "Unsupported ATOM layout; using original VBIOS");
        else KernelLog::logWarning(KernelLog::Category::Hardware, "VBIOS", "augment", "Unable to allocate buffer; using original VBIOS");
        bool published = device->setProperty("ATY,bin_image", rom);
        KernelLog::logInfo(KernelLog::Category::Hardware, "VBIOS", "publish", "VFCT image size=%u published=%u", rom->getLength(), static_cast<unsigned int>(published));
        rom->release();
        return published;
    } else {
        KernelLog::logWarning(KernelLog::Category::Hardware, "VBIOS", "publish", "No valid VFCT image for device %04x", deviceId);
    }
    return false;
}
#endif

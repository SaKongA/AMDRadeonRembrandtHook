#include "Hardware/Rembrandt/PciIdentity.hpp"
#include "Diagnostics/KernelLog.hpp"
#include <IOKit/IORegistryEntry.h>
#include <libkern/c++/OSData.h>
#include <libkern/libkern.h>

namespace {

void readCpuId(uint32_t leaf, uint32_t *registers) {
    __asm__ volatile("cpuid" : "=a"(registers[0]), "=b"(registers[1]),
        "=c"(registers[2]), "=d"(registers[3]) : "a"(leaf), "c"(0));
}

bool matchesCpuModel(const char *brand, const char *model) {
    size_t length = strlen(model);
    for (const char *p = brand; *p; ++p) {
        if ((p == brand || p[-1] == ' ') && !strncmp(p, model, length) &&
            (p[length] == '\0' || p[length] == ' ')) return true;
    }
    return false;
}

const char *getRembrandtModelName() {
    uint32_t registers[4] {};
    readCpuId(0x80000000, registers);
    if (registers[0] < 0x80000004) return "Graphics (Rembrandt)";
    char brand[49] {};
    for (uint32_t i = 0; i < 3; ++i) {
        readCpuId(0x80000002 + i, registers);
        memcpy(brand + i * 16, registers, sizeof(registers));
    }
    if (strncmp(brand, "AMD ", 4)) return "Graphics (Rembrandt)";
    constexpr const char *models680[] {
        "6800H", "6800HS", "6800U", "6900HS", "6900HX", "6980HS", "6980HX", "7735HS", "7735U"
    };
    constexpr const char *models660[] {
        "6600H", "6600HS", "6600U", "7335U", "7535HS", "7535U"
    };
    for (auto model : models680) if (matchesCpuModel(brand, model)) return "680M";
    for (auto model : models660) if (matchesCpuModel(brand, model)) return "660M";
    return "Graphics (Rembrandt)";
}

}

bool publishRembrandtIdentity(IORegistryEntry *device, uint32_t deviceId) {
    if (!device || !isRembrandtDeviceId(deviceId)) return false;
    const uint32_t spoofedDeviceId = 0x73ff;
    auto data = OSData::withBytes(&spoofedDeviceId, sizeof(spoofedDeviceId));
    bool published = data && device->setProperty("device-id", data);
    if (data) data->release();
    KernelLog::logInfo(KernelLog::Category::Hardware, "PCI", "publish identity", "Device %04x -> %04x published=%u", deviceId, spoofedDeviceId, static_cast<unsigned int>(published));
    return published;
}

bool publishRembrandtModel(IORegistryEntry *device) {
    if (!device) return false;
    const char *name = getRembrandtModelName();
    char model[64] {};
    int length = snprintf(model, sizeof(model), "AMD Radeon %s", name);
    if (length < 0 || static_cast<size_t>(length) >= sizeof(model)) return false;
    auto data = OSData::withBytes(model, static_cast<unsigned int>(length) + 1);
    bool published = data && device->setProperty("model", data);
    if (data) data->release();
    bool familyPublished = device->setProperty("ATY,FamilyName", "Radeon");
    bool namePublished = device->setProperty("ATY,DeviceName", name);
    if (published && familyPublished && namePublished)
        KernelLog::logInfo(KernelLog::Category::Hardware, "PCI", "publish model", "Published %s", model);
    else KernelLog::logError(KernelLog::Category::Hardware, "PCI", "publish model", "Model=%u family=%u name=%u",
        static_cast<unsigned int>(published), static_cast<unsigned int>(familyPublished), static_cast<unsigned int>(namePublished));
    return published && familyPublished && namePublished;
}

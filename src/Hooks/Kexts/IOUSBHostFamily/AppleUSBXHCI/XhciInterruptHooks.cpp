#include "Kexts/IOUSBHostFamily/AppleUSBXHCI/XhciInterruptHooks.hpp"
#include "Diagnostics/KernelLog.hpp"
#include "Hardware/Rembrandt/PciIdentity.hpp"
#include <Headers/kern_api.hpp>
#include <Headers/kern_mach.hpp>
#include <mach-o/loader.h>

namespace {

struct InterruptTargetPattern {
    uint8_t bytes[16];
    uint8_t length;
    uint8_t targetOffset;
};

constexpr InterruptTargetPattern patterns[] {
    {{0x48, 0x89, 0x01, 0xc7, 0x41, 0x08, 0x08, 0x00, 0x40, 0x00}, 10, 8},
    {{0xc7, 0x44, 0x08, 0x08, 0x00, 0x00, 0x40, 0x00, 0xc7, 0x44, 0x08, 0x0c, 0x21, 0x04, 0x00, 0x00}, 16, 6},
    {{0x44, 0x09, 0xf1, 0x81, 0xc9, 0x00, 0x00, 0x40, 0x00, 0x42, 0x89, 0x4c, 0x2a, 0x08}, 14, 7},
    {{0x48, 0xc7, 0x04, 0x18, 0x00, 0x00, 0x00, 0x00, 0xc7, 0x44, 0x18, 0x08, 0x00, 0x00, 0x40, 0x00}, 16, 14},
    {{0xc7, 0x44, 0x08, 0x08, 0x00, 0x00, 0x40, 0x00, 0xc7, 0x44, 0x08, 0x0c, 0x00, 0x18, 0x00, 0x00}, 16, 6},
    {{0x41, 0xb8, 0x00, 0x00, 0x40, 0x00, 0x44, 0x89, 0x44, 0x3e, 0x08}, 11, 4},
};

struct VerifiedSite {
    const char *name;
    uint32_t offset;
    uint8_t length, byteOffset, from;
    uint8_t bytes[17];
};

// Both supported builds have the same verified instruction sites. 25G332 was
// checked against the AppleUSBXHCI image extracted from its Boot KC.
constexpr VerifiedSite sites660[] {
    {"setup",  0x1b79, 10, 8, 0x40, {0x48,0x89,0x01,0xc7,0x41,0x08,0x08,0x00,0x40,0x00}},
    {"zlp",    0x20c4, 16, 6, 0x40, {0xc7,0x44,0x08,0x08,0x00,0x00,0x40,0x00,0xc7,0x44,0x08,0x0c,0x21,0x04,0x00,0x00}},
    {"data",   0x2320, 12, 3, 0x40, {0xb9,0x00,0x00,0x40,0x00,0x4c,0x89,0xad,0xc0,0xfe,0xff,0xff}},
    {"tdsize", 0x2347, 17,13, 0x20, {0x83,0xf9,0x1f,0xb8,0x1f,0x00,0x00,0x00,0x0f,0x43,0xc8,0x83,0xc9,0x20,0xc1,0xe1,0x11}},
    {"status", 0x2581, 16,14, 0x40, {0x48,0xc7,0x04,0x18,0x00,0x00,0x00,0x00,0xc7,0x44,0x18,0x08,0x00,0x00,0x40,0x00}},
    {"link",   0x25da, 16, 6, 0x40, {0xc7,0x44,0x08,0x08,0x00,0x00,0x40,0x00,0xc7,0x44,0x08,0x0c,0x00,0x18,0x00,0x00}},
    {"sparse", 0x2eba5,11, 4, 0x40, {0x41,0xb8,0x00,0x00,0x40,0x00,0x44,0x89,0x44,0x3e,0x08}},
};

bool verified660Image(const uint8_t *bytes, size_t size) {
    constexpr uint8_t uuid229[] {0x4f,0x5e,0x24,0xc5,0x4f,0xea,0x32,0xda,0xa8,0xad,0xd2,0xfe,0xfe,0xf3,0xab,0xf2};
    constexpr uint8_t uuid332[] {0x1a,0xdb,0xc3,0xde,0x44,0x02,0x39,0x3b,0xa4,0x2c,0x0f,0x58,0xc5,0x75,0x06,0x98};
    if (size < sizeof(mach_header_64)) return false;
    auto header = reinterpret_cast<const mach_header_64 *>(bytes);
    if (header->magic != MH_MAGIC_64 || header->ncmds > 64 || header->sizeofcmds > size - sizeof(*header)) return false;
    size_t cursor = sizeof(*header), end = cursor + header->sizeofcmds;
    bool uuidOk = false, textOk = false;
    for (uint32_t i = 0; i < header->ncmds; ++i) {
        if (cursor > end || end - cursor < sizeof(load_command)) return false;
        auto command = reinterpret_cast<const load_command *>(bytes + cursor);
        if (command->cmdsize < sizeof(load_command) || command->cmdsize > end - cursor) return false;
        if (command->cmd == LC_UUID && command->cmdsize >= sizeof(uuid_command)) {
            auto uuid = reinterpret_cast<const uuid_command *>(command)->uuid;
            uuidOk = !memcmp(uuid, uuid229, sizeof(uuid229)) || !memcmp(uuid, uuid332, sizeof(uuid332));
        } else if (command->cmd == LC_SEGMENT_64 && command->cmdsize >= sizeof(segment_command_64)) {
            auto segment = reinterpret_cast<const segment_command_64 *>(command);
            if (!strncmp(segment->segname, "__TEXT", sizeof(segment->segname))) textOk = segment->vmsize == 0x3b000;
        }
        cursor += command->cmdsize;
    }
    return uuidOk && textOk;
}

void patch660InterruptTargets(uint8_t *bytes, size_t size) {
    if (checkKernelArgument("-rmbxhciintr1")) {
        KernelLog::logInfo(KernelLog::Category::Hook, "XHCI", "660M patch", "Disabled by -rmbxhciintr1");
        return;
    }
    if (!verified660Image(bytes, size)) {
        KernelLog::logWarning(KernelLog::Category::Hook, "XHCI", "660M patch", "Unsupported AppleUSBXHCI image; no changes applied");
        return;
    }
    size_t pending = 0, done = 0;
    for (const auto &site : sites660) {
        if (site.offset > size || site.length > size - site.offset) return;
        auto p = bytes + site.offset;
        for (uint8_t i = 0; i < site.length; ++i) {
            if (i != site.byteOffset && p[i] != site.bytes[i]) {
                KernelLog::logWarning(KernelLog::Category::Hook, "XHCI", "660M patch", "Site %s differs; no changes applied", site.name);
                return;
            }
        }
        if (p[site.byteOffset] == site.from) ++pending;
        else if (!p[site.byteOffset]) ++done;
        else {
            KernelLog::logWarning(KernelLog::Category::Hook, "XHCI", "660M patch", "Site %s has unexpected target; no changes applied", site.name);
            return;
        }
    }
    if (pending && done) {
        KernelLog::logWarning(KernelLog::Category::Hook, "XHCI", "660M patch", "Mixed patch state; no changes applied");
        return;
    }
    if (!pending) {
        KernelLog::logInfo(KernelLog::Category::Hook, "XHCI", "660M patch", "All %lu targets already patched", static_cast<unsigned long>(done));
        return;
    }
    if (MachInfo::setKernelWriting(true, KernelPatcher::kernelWriteLock) != KERN_SUCCESS) {
        KernelLog::logError(KernelLog::Category::Hook, "XHCI", "660M patch", "Unable to enable kernel writing");
        return;
    }
    for (const auto &site : sites660) bytes[site.offset + site.byteOffset] = 0;
    auto result = MachInfo::setKernelWriting(false, KernelPatcher::kernelWriteLock);
    size_t verified = 0;
    for (const auto &site : sites660) verified += bytes[site.offset + site.byteOffset] == 0;
    KernelLog::logInfo(KernelLog::Category::Hook, "XHCI", "660M patch", "Patched %lu/%lu targets; write protection result=%d", static_cast<unsigned long>(verified), static_cast<unsigned long>(pending), result);
}

}

void patchXhciInterruptTargets(mach_vm_address_t address, size_t size) {
    auto bytes = reinterpret_cast<uint8_t *>(address);
    if (isRembrandt660M()) {
        patch660InterruptTargets(bytes, size);
        return;
    }
    size_t count = 0;
    if (MachInfo::setKernelWriting(true, KernelPatcher::kernelWriteLock) != KERN_SUCCESS) {
        KernelLog::logError(KernelLog::Category::Hook, "XHCI", "patch", "Unable to enable kernel writing");
        return;
    }
    for (const auto &pattern : patterns) {
        for (size_t i = 0; i + pattern.length <= size; ++i) {
            if (!memcmp(bytes + i, pattern.bytes, pattern.length)) {
                bytes[i + pattern.targetOffset] = 0;
                ++count;
            }
        }
    }
    MachInfo::setKernelWriting(false, KernelPatcher::kernelWriteLock);
    if (count) KernelLog::logInfo(KernelLog::Category::Hook, "XHCI", "patch", "Patched %lu interrupt targets", static_cast<unsigned long>(count));
    else KernelLog::logWarning(KernelLog::Category::Hook, "XHCI", "patch", "No interrupt target patterns found");
}

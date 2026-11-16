#include "Kexts/AMDRadeonX6000Framebuffer/FramebufferHooks.hpp"
#include "Diagnostics/KernelLog.hpp"
#include <IOKit/IOLib.h>
#include <IOKit/graphics/IOFramebuffer.h>
#include <IOKit/graphics/IOGraphicsTypes.h>
#include <IOKit/IOService.h>
#include <libkern/c++/OSData.h>

namespace {

mach_vm_address_t getDisplayModeInformation;
using GetDisplayModeInformation = IOReturn (*)(void *, IODisplayModeID, IODisplayModeInformation *);

bool getEdidNativeSize(IORegistryEntry *entry, uint32_t &width, uint32_t &height) {
    auto data = OSDynamicCast(OSData, entry->getProperty(kIODisplayEDIDKey));
    if (!data || data->getLength() < 128) return false;
    auto bytes = static_cast<const uint8_t *>(data->getBytesNoCopy());
    constexpr uint8_t header[] {0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00};
    if (memcmp(bytes, header, sizeof(header)) || !(bytes[24] & 2) || !(bytes[54] | bytes[55])) return false;
    uint8_t checksum = 0;
    for (size_t i = 0; i < 128; ++i) checksum += bytes[i];
    if (checksum) return false;
    width = bytes[56] | ((bytes[58] & 0xf0u) << 4);
    height = bytes[59] | ((bytes[61] & 0xf0u) << 4);
    return width && height;
}

bool getNativeDisplaySize(IOFramebuffer *framebuffer, uint32_t &width, uint32_t &height) {
    if (getEdidNativeSize(framebuffer, width, height)) return true;
    auto children = framebuffer->getChildIterator(gIOServicePlane);
    if (children) {
        while (auto child = OSDynamicCast(IORegistryEntry, children->getNextObject())) {
            if (getEdidNativeSize(child, width, height)) {
                children->release();
                return true;
            }
        }
        children->release();
    }
    IOItemCount count = framebuffer->getDisplayModeCount();
    if (!count || count > 4096) return false;
    size_t bytes = static_cast<size_t>(count) * sizeof(IODisplayModeID);
    auto modes = static_cast<IODisplayModeID *>(IOMalloc(bytes));
    if (!modes) return false;
    bool found = false;
    if (framebuffer->getDisplayModes(modes) == kIOReturnSuccess) {
        auto getInformation = reinterpret_cast<GetDisplayModeInformation>(getDisplayModeInformation);
        for (IOItemCount i = 0; i < count; ++i) {
            IODisplayModeInformation information {};
            if (getInformation(framebuffer, modes[i], &information) != kIOReturnSuccess ||
                !(information.flags & kDisplayModeNativeFlag) || !information.nominalWidth || !information.nominalHeight) continue;
            width = information.nominalWidth;
            height = information.nominalHeight;
            found = true;
            break;
        }
    }
    IOFree(modes, bytes);
    return found;
}

IOReturn wrapConnectFlags(void *framebuffer, IOIndex, IODisplayModeID mode, UInt32 *flags) {
    *flags = kDisplayModeValidFlag | kDisplayModeSafeFlag;
    if (!getDisplayModeInformation) return kIOReturnSuccess;
    auto service = static_cast<IOService *>(framebuffer);
    const char *location = service->getLocation();
    if (!location || strcmp(location, "0")) return kIOReturnSuccess;

    IODisplayModeInformation info {};
    auto getInformation = reinterpret_cast<GetDisplayModeInformation>(getDisplayModeInformation);
    auto result = getInformation(framebuffer, mode, &info);
    uint32_t nativeWidth = 0, nativeHeight = 0;
    if (result == kIOReturnSuccess && info.nominalWidth && info.nominalHeight &&
        getNativeDisplaySize(static_cast<IOFramebuffer *>(service), nativeWidth, nativeHeight) &&
        static_cast<uint64_t>(info.nominalWidth) * nativeHeight != static_cast<uint64_t>(info.nominalHeight) * nativeWidth) {
        *flags |= kDisplayModeNeverShowFlag;
    }
    return kIOReturnSuccess;
}

}

void installDisplayModeHooks(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
    auto function = patcher.solveSymbol(index,
        "__ZN35AMDRadeonX6000_AmdRadeonFramebuffer12connectFlagsEiiPj", address, size);
    if (!function) KernelLog::logWarning(KernelLog::Category::Hook, "DisplayMode", "resolve", "connectFlags unavailable: error=%d", static_cast<int>(patcher.getError()));
    patcher.clearError();
    getDisplayModeInformation = patcher.solveSymbol(index,
        "__ZN35AMDRadeonX6000_AmdRadeonFramebuffer28getInformationForDisplayModeEiP24IODisplayModeInformation",
        address, size);
    if (!getDisplayModeInformation) KernelLog::logWarning(KernelLog::Category::Hook, "DisplayMode", "resolve", "Native mode lookup unavailable: error=%d", static_cast<int>(patcher.getError()));
    patcher.clearError();

    constexpr uint8_t emptyStub[] {0x55, 0x48, 0x89, 0xe5, 0x31, 0xc0, 0x5d, 0xc3};
    auto bytes = reinterpret_cast<uint8_t *>(function);
    if (!bytes || memcmp(bytes, emptyStub, sizeof(emptyStub))) {
        KernelLog::logWarning(KernelLog::Category::Hook, "DisplayMode", "install", "connectFlags absent or does not match expected stub");
        return;
    }
    KernelPatcher::RouteRequest request {
        "__ZN35AMDRadeonX6000_AmdRadeonFramebuffer12connectFlagsEiiPj", wrapConnectFlags
    };
    if (patcher.routeMultiple(index, &request, 1, address, size)) KernelLog::logInfo(KernelLog::Category::Hook, "DisplayMode", "install", "Native aspect ratio hook installed");
    else KernelLog::logError(KernelLog::Category::Hook, "DisplayMode", "install", "Hook failed: error=%d", static_cast<int>(patcher.getError()));
    patcher.clearError();
}

#include "Kexts/AppleBacklight/AppleBacklightPanelHooks.hpp"
#include "Diagnostics/KernelLog.hpp"
#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSNumber.h>

namespace {

using SetDisplayFunction = bool (*)(IOService *, IOService *);
constexpr size_t panelUidOffset = 0x108;
constexpr uint32_t maximumBacklight = 0xffff;
constexpr uint16_t panelCurve[] {
    0x0000, 0x002f, 0x0036, 0x0040, 0x004b, 0x0058, 0x0067, 0x0077, 0x008b,
    0x00af, 0x00dd, 0x0118, 0x0160, 0x01ba, 0x022c, 0x02bf, 0x0375
};
mach_vm_address_t originalSetDisplay;
OSData *panelCurveData;

void addPanelBrightnessEntry(IOService *panel, IOService *display) {
    auto productId = OSDynamicCast(OSNumber, display->getProperty("DisplayProductID"));
    if (!productId) return;
    uint32_t uid = *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(panel) + panelUidOffset);
    char key[32] {};
    snprintf(key, sizeof(key), "F%uT%04x", uid, productId->unsigned32BitValue());
    auto existing = OSDynamicCast(OSDictionary, panel->getProperty("ApplePanels"));
    if ((!existing && panel->getProperty("ApplePanel")) || (existing && existing->getObject(key))) return;
    auto panels = existing ? OSDictionary::withDictionary(existing, existing->getCount() + 1) : OSDictionary::withCapacity(1);
    if (!panels) return;
    constexpr size_t count = sizeof(panelCurve) / sizeof(panelCurve[0]);
    uint8_t bytes[2 + 2 * count] {0, count};
    for (size_t i = 0; i < count; ++i) {
        uint32_t value = panelCurve[i] * maximumBacklight / panelCurve[count - 1];
        bytes[2 + 2 * i] = static_cast<uint8_t>(value >> 8);
        bytes[3 + 2 * i] = value & 0xff;
    }
    if (!panelCurveData) panelCurveData = OSData::withBytes(bytes, sizeof(bytes));
    if (panelCurveData) {
        panels->setObject(key, panelCurveData);
        panel->setProperty("ApplePanels", panels);
        KernelLog::logInfo(KernelLog::Category::Hook, "AppleBacklight", "configure panel", "Submitted brightness curve: key=%s points=%lu", key, static_cast<unsigned long>(count));
    }
    else KernelLog::logError(KernelLog::Category::Hook, "AppleBacklight", "configure panel", "Unable to allocate brightness curve: key=%s", key);
    panels->release();
}

bool wrapSetPanelDisplay(IOService *panel, IOService *display) {
    if (panel && display) addPanelBrightnessEntry(panel, display);
    return reinterpret_cast<SetDisplayFunction>(originalSetDisplay)(panel, display);
}

}

void installAppleBacklightPanelHook(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
    KernelPatcher::RouteRequest request {
        "__ZN15AppleIntelPanel10setDisplayEP9IODisplay", wrapSetPanelDisplay, originalSetDisplay
    };
    if (patcher.routeMultiple(index, &request, 1, address, size)) KernelLog::logInfo(KernelLog::Category::Hook, "AppleBacklight", "install", "Panel hook installed");
    else KernelLog::logError(KernelLog::Category::Hook, "AppleBacklight", "install", "Panel hook failed: error=%d", static_cast<int>(patcher.getError()));
    patcher.clearError();
}

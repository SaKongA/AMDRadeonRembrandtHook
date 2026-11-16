#include "Kexts/AMDRadeonX6000Framebuffer/FramebufferHooks.hpp"
#include "Diagnostics/KernelLog.hpp"
#include "Hardware/Rembrandt/Display/PanelBacklight.hpp"
#include <IOKit/IOService.h>

extern uint32_t mmioRead(uint32_t reg);

namespace {

constexpr uint32_t brightnessAttribute = 'brgt';
constexpr uint32_t parametersAttribute = 'parm';
constexpr uint32_t backlightAttribute = 'bklt';
constexpr uint32_t brightnessMaximum = 1024;
constexpr uint32_t backlightMaximum = 0xffff;
constexpr uint32_t pwmControlDword = 0x34c0 + 0x2f19;
constexpr uint32_t pwmPeriodDword = pwmControlDword + 2;

using GetAttribute = IOReturn (*)(IOService *, int32_t, uint32_t, uintptr_t *);
using SetAttribute = IOReturn (*)(IOService *, int32_t, uint32_t, uintptr_t);

mach_vm_address_t originalGetAttribute;
mach_vm_address_t originalSetAttribute;
uint32_t cachedBrightness = ~0u;

bool isPanelFramebuffer(IOService *framebuffer) {
    static IOService *panel;
    if (panel) return framebuffer == panel;
    auto location = framebuffer->getLocation();
    if (location && !strcmp(location, "0")) {
        panel = framebuffer;
        return true;
    }
    return false;
}

uint32_t getCurrentBrightness() {
    if (cachedBrightness <= brightnessMaximum) return cachedBrightness;
    uint32_t periodRegister = mmioRead(pwmPeriodDword);
    uint32_t bits = (periodRegister >> 16) & 0x1f;
    if (!bits || bits > 16) bits = 16;
    uint32_t period = periodRegister & ((1u << bits) - 1);
    if (!period) return brightnessMaximum;
    uint32_t active = (mmioRead(pwmControlDword) & 0xffff) >> (16 - bits);
    uint32_t value = static_cast<uint32_t>((static_cast<uint64_t>(active) * brightnessMaximum + period / 2) / period);
    return value > brightnessMaximum ? brightnessMaximum : value;
}

IOReturn wrapGetAttribute(IOService *framebuffer, int32_t connection, uint32_t attribute, uintptr_t *value) {
    if (attribute == backlightAttribute && connection == 0 && value && isPanelFramebuffer(framebuffer)) {
        value[0] = getCurrentBrightness() * backlightMaximum / brightnessMaximum;
        value[1] = 0;
        value[2] = backlightMaximum;
        return kIOReturnSuccess;
    }
    if (attribute == brightnessAttribute && connection == 0 && value && isPanelFramebuffer(framebuffer)) {
        value[0] = getCurrentBrightness();
        value[1] = 0;
        value[2] = brightnessMaximum;
        return kIOReturnSuccess;
    }
    auto result = reinterpret_cast<GetAttribute>(originalGetAttribute)(framebuffer, connection, attribute, value);
    if (attribute == parametersAttribute && connection == 0 && result == kIOReturnSuccess &&
        value && !value[1] && isPanelFramebuffer(framebuffer)) value[1] = brightnessAttribute;
    return result;
}

IOReturn wrapSetAttribute(IOService *framebuffer, int32_t connection, uint32_t attribute, uintptr_t value) {
    if (attribute == backlightAttribute && connection == 0 && isPanelFramebuffer(framebuffer)) {
        uint32_t level = value > backlightMaximum ? backlightMaximum : static_cast<uint32_t>(value);
        cachedBrightness = (level * brightnessMaximum + backlightMaximum / 2) / backlightMaximum;
        setBacklight(static_cast<uint32_t>(static_cast<uint64_t>(level) * 0x10000 / backlightMaximum));
        return kIOReturnSuccess;
    }
    if (attribute == brightnessAttribute && connection == 0 && isPanelFramebuffer(framebuffer)) {
        cachedBrightness = value > brightnessMaximum ? brightnessMaximum : static_cast<uint32_t>(value);
        setBacklight(static_cast<uint32_t>(static_cast<uint64_t>(cachedBrightness) * 0x10000 / brightnessMaximum));
        return kIOReturnSuccess;
    }
    return reinterpret_cast<SetAttribute>(originalSetAttribute)(framebuffer, connection, attribute, value);
}

}

void installFramebufferBrightnessHooks(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
    KernelPatcher::RouteRequest requests[] {
        {"__ZN35AMDRadeonX6000_AmdRadeonFramebuffer25getAttributeForConnectionEijPm", wrapGetAttribute, originalGetAttribute},
        {"__ZN35AMDRadeonX6000_AmdRadeonFramebuffer25setAttributeForConnectionEijm", wrapSetAttribute, originalSetAttribute},
    };
    if (patcher.routeMultiple(index, requests, address, size)) KernelLog::logInfo(KernelLog::Category::Hook, "Backlight", "install framebuffer", "Brightness hooks installed");
    else KernelLog::logError(KernelLog::Category::Hook, "Backlight", "install framebuffer", "Brightness hooks failed: error=%d", static_cast<int>(patcher.getError()));
    patcher.clearError();
}

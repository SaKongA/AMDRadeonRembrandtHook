#include "Kernel/KextLoadHooks.hpp"
#include "Diagnostics/KernelLog.hpp"
#include "Kernel/SystemPowerHooks.hpp"
#include "Kernel/DriverMatchingHooks.hpp"
#include "Bundles/AMDRadeonX6000MTLDriver/MetalSwizzleHooks.hpp"
#include "Kexts/AMDRadeonX6000/AcceleratorHooks.hpp"
#include "Kexts/AMDRadeonX6000Framebuffer/FramebufferHooks.hpp"
#include "Kexts/AMDRadeonX6000HWServices/AMDRadeonX6000HWLibs/HWLibsHooks.hpp"
#include "Kexts/AppleBacklight/AppleBacklightPanelHooks.hpp"
#include "Kexts/IOPCIFamily/PciConfigHooks.hpp"
#include "Kexts/IOUSBHostFamily/AppleUSBXHCI/XhciInterruptHooks.hpp"
#include <Headers/kern_api.hpp>
#include <mach/kmod.h>

namespace {

const char *hwLibsPaths[] {"/System/Library/Extensions/AMDRadeonX6000HWLibs.kext/Contents/MacOS/AMDRadeonX6000HWLibs"};
const char *framebufferPaths[] {"/System/Library/Extensions/AMDRadeonX6000Framebuffer.kext/Contents/MacOS/AMDRadeonX6000Framebuffer"};
const char *acceleratorPaths[] {"/System/Library/Extensions/AMDRadeonX6000.kext/Contents/MacOS/AMDRadeonX6000"};
const char *hwServicesPaths[] {"/System/Library/Extensions/AMDRadeonX6000HWServices.kext/Contents/MacOS/AMDRadeonX6000HWServices"};
const char *backlightPaths[] {"/System/Library/Extensions/AppleBacklight.kext/Contents/MacOS/AppleBacklight"};
const char *pciPaths[] {"/System/Library/Extensions/IOPCIFamily.kext/Contents/MacOS/IOPCIFamily"};

KernelPatcher::KextInfo kexts[] {
    {"com.apple.kext.AMDRadeonX6000HWLibs", hwLibsPaths, arrsize(hwLibsPaths), {true}, {}, KernelPatcher::KextInfo::Unloaded},
    {"com.apple.kext.AMDRadeonX6000Framebuffer", framebufferPaths, arrsize(framebufferPaths), {true}, {}, KernelPatcher::KextInfo::Unloaded},
    {"com.apple.kext.AMDRadeonX6000", acceleratorPaths, arrsize(acceleratorPaths), {true}, {}, KernelPatcher::KextInfo::Unloaded},
    {"com.apple.kext.AMDRadeonX6000HWServices", hwServicesPaths, arrsize(hwServicesPaths), {true}, {}, KernelPatcher::KextInfo::Unloaded},
    {"com.apple.driver.AppleBacklight", backlightPaths, arrsize(backlightPaths), {true}, {}, KernelPatcher::KextInfo::Unloaded},
    {"com.apple.iokit.IOPCIFamily", pciPaths, arrsize(pciPaths), {true}, {}, KernelPatcher::KextInfo::Unloaded},
};

constexpr size_t hwLibsIndex = 0, framebufferIndex = 1, acceleratorIndex = 2, backlightIndex = 4, pciIndex = 5;
KernelPatcher *kernelPatcher;
kmod_info_t **loadedKmods;
mach_vm_address_t originalKextStart;
bool installed[arrsize(kexts)];
bool agdcFound, xhciFound;
bool pciInstallAttempted;

void installFramebufferHooks(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
    patchFramebufferCapabilities(address, size);
    installInterruptRingHooks(patcher, address, size);
    installDisplayModeHooks(patcher, index, address, size);
    bool dalReady = initializeDalRegisterHooks();
    if (!dalReady) KernelLog::logError(KernelLog::Category::Hook, "Framebuffer", "initialize DAL", "Register hooks unavailable");
    prepareDisplayRecovery(patcher, index, address, size);
    if (dalReady) installDalRegisterHooks(patcher, index, address, size);
    installDisplayRecoveryHook(patcher, index, address, size);
    if (dalReady) installRawRegisterHooks(patcher, index, address, size);
    installFramebufferBrightnessHooks(patcher, index, address, size);
    installDisplayPresenterHooks(patcher, index, address, size);
    installDisplayStreamHook(patcher, address, size);
    installFramebufferIdentityHooks(patcher, index, address, size);
}

void scanLoadedKexts() {
    if (!kernelPatcher || !loadedKmods) return;
    if (!__atomic_load_n(&pciInstallAttempted, __ATOMIC_ACQUIRE) &&
        kexts[pciIndex].loadIndex != KernelPatcher::KextInfo::Unloaded) {
        for (auto module = *loadedKmods; module; module = module->next) {
            if (strncmp(module->name, kexts[pciIndex].id, KMOD_MAX_NAME)) continue;
            bool expected = false;
            if (!__atomic_compare_exchange_n(&pciInstallAttempted, &expected, true, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) break;
            KernelLog::logInfo(KernelLog::Category::Hook, "KextLoad", "install", "Installing PCI hooks before graphics initialization");
            auto size = kernelPatcher->updateRunningInfo(kexts[pciIndex].loadIndex, module->address, module->size);
            installed[pciIndex] = installPciConfigHooks(*kernelPatcher, kexts[pciIndex].loadIndex, module->address, size);
            break;
        }
    }
    for (auto module = *loadedKmods; module; module = module->next) {
        if (!agdcFound && !strncmp(module->name, "com.apple.AppleGraphicsDeviceControl", KMOD_MAX_NAME)) {
            agdcFound = true;
            KernelLog::logInfo(KernelLog::Category::Hook, "KextLoad", "detect", "Found %s", module->name);
            setDisplayRecoveryAgdcAddress(module->address);
        }
        if (!xhciFound && !strncmp(module->name, "com.apple.driver.usb.AppleUSBXHCI", KMOD_MAX_NAME)) {
            xhciFound = true;
            KernelLog::logInfo(KernelLog::Category::Hook, "KextLoad", "detect", "Found %s", module->name);
            patchXhciInterruptTargets(module->address, module->size);
        }
        for (size_t index = 0; index < 3; ++index) {
            if (installed[index] || strncmp(module->name, kexts[index].id, KMOD_MAX_NAME) ||
                kexts[index].loadIndex == KernelPatcher::KextInfo::Unloaded) continue;
            installed[index] = true;
            KernelLog::logInfo(KernelLog::Category::Hook, "KextLoad", "install", "Installing hooks for %s", module->name);
            if (!arePciConfigHooksInstalled())
                KernelLog::logError(KernelLog::Category::Hook, "KextLoad", "install", "PCI identity hooks unavailable before %s", module->name);
            auto size = kernelPatcher->updateRunningInfo(kexts[index].loadIndex, module->address, module->size);
            if (index == hwLibsIndex)
                installHwLibsHooks(*kernelPatcher, kexts[index].loadIndex, module->address, size);
            else if (index == framebufferIndex)
                installFramebufferHooks(*kernelPatcher, kexts[index].loadIndex, module->address, size);
            else if (index == acceleratorIndex)
                installAcceleratorHooks(*kernelPatcher, kexts[index].loadIndex, module->address, size);
        }
        if (!installed[backlightIndex] && !strncmp(module->name, kexts[backlightIndex].id, KMOD_MAX_NAME) &&
            kexts[backlightIndex].loadIndex != KernelPatcher::KextInfo::Unloaded) {
            installed[backlightIndex] = true;
            KernelLog::logInfo(KernelLog::Category::Hook, "KextLoad", "install", "Installing hooks for %s", module->name);
            auto size = kernelPatcher->updateRunningInfo(kexts[backlightIndex].loadIndex, module->address, module->size);
            installAppleBacklightPanelHook(*kernelPatcher, kexts[backlightIndex].loadIndex, module->address, size);
        }
    }
}

OSReturn wrapKextStart(void *kext, bool startDependencies) {
    scanLoadedKexts();
    return reinterpret_cast<OSReturn (*)(void *, bool)>(originalKextStart)(kext, startDependencies);
}

void installKextStartHook(void *, KernelPatcher &patcher) {
    kernelPatcher = &patcher;
    loadedKmods = reinterpret_cast<kmod_info_t **>(patcher.solveSymbol(KernelPatcher::KernelID, "_kmod"));
    if (!loadedKmods) KernelLog::logError(KernelLog::Category::Hook, "KextLoad", "resolve", "_kmod unavailable: error=%d", static_cast<int>(patcher.getError()));
    patcher.clearError();
    patcher.loadKinfo(&kexts[pciIndex]);
    if (kexts[pciIndex].loadIndex == KernelPatcher::KextInfo::Unloaded)
        KernelLog::logError(KernelLog::Category::Hook, "KextLoad", "prepare PCI", "IOPCIFamily symbols unavailable: error=%d", static_cast<int>(patcher.getError()));
    patcher.clearError();
    scanLoadedKexts();
    if (!arePciConfigHooksInstalled())
        KernelLog::logWarning(KernelLog::Category::Hook, "KextLoad", "prepare PCI", "PCI hooks not yet installed; retrying on kext start");
    installDriverMatchingHooks(patcher);
    KernelPatcher::RouteRequest request {"__ZN6OSKext5startEb", wrapKextStart, originalKextStart};
    if (patcher.routeMultiple(KernelPatcher::KernelID, &request, 1))
        KernelLog::logInfo(KernelLog::Category::Hook, "KextLoad", "install", "OSKext start hook installed");
    else KernelLog::logError(KernelLog::Category::Hook, "KextLoad", "install", "OSKext start hook failed: error=%d", static_cast<int>(patcher.getError()));
    patcher.clearError();
    installSystemPowerHooks(patcher);
    installMetalSwizzleHooks(patcher);
}

}

void registerKextLoadHooks() {
    KernelLog::logInfo(KernelLog::Category::Hook, "KextLoad", "register", "Registering %lu watched kexts", static_cast<unsigned long>(arrsize(kexts)));
    lilu.onKextLoadForce(kexts, arrsize(kexts));
    lilu.onPatcherLoadForce(installKextStartHook);
}

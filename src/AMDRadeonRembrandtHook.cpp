#include "Kernel/GpuInterruptStartup.hpp"
#include "Diagnostics/KernelLog.hpp"
#include "Kernel/KextLoadHooks.hpp"
#include "Hardware/USB4/Usb4Power.hpp"
#include <Headers/plugin_start.hpp>
#include <Headers/kern_api.hpp>
#include <Headers/kern_nvram.hpp>
#include <Headers/kern_efi.hpp>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IONVRAM.h>
#include <libkern/c++/OSString.h>

extern void *gPEEFIRuntimeServices;

namespace {

constexpr const char *enableKey = NVRAM_PREFIX(NVRAM_APPLE_BOOT_GUID, "AMDRadeonRembrandtHookEnabled");
constexpr const char *testKey = NVRAM_PREFIX(NVRAM_APPLE_BOOT_GUID, "AMDRadeonRembrandtHookTest");
constexpr EFI_GUID bootGuid {0x7c436110, 0xab2a, 0x4bbb, {0xa8, 0x80, 0xfe, 0x41, 0x99, 0x5c, 0x9f, 0x82}};
constexpr const char16_t *enableEfiKey = u"AMDRadeonRembrandtHookEnabled";
constexpr const char16_t *testEfiKey = u"AMDRadeonRembrandtHookTest";

enum class EfiFlagState { Missing, Disabled, Enabled, Invalid };

EfiFlagState readEfiFlag(EfiRuntimeServices *services, const char16_t *name) {
    uint8_t value = 0xff;
    uint32_t attributes = 0;
    uint64_t size = sizeof(value);
    uint64_t status = services->getVariable(name, &bootGuid, &attributes, &size, &value);
    if (status == EFI_ERROR64(EFI_NOT_FOUND)) return EfiFlagState::Missing;
    if (status != EFI_SUCCESS || size != sizeof(value) || value > 1) return EfiFlagState::Invalid;
    return value == 1 ? EfiFlagState::Enabled : EfiFlagState::Disabled;
}

bool authorizeEfiStartup() {
    auto matching = IOService::serviceMatching("AppleEFIRuntime");
    if (!matching) {
        KernelLog::logError(KernelLog::Category::Core, "Core", "read EFI", "Unable to check EFI runtime ownership; startup skipped");
        return false;
    }
    auto appleRuntime = IOService::copyMatchingService(matching);
    matching->release();
    if (appleRuntime) {
        appleRuntime->release();
        KernelLog::logError(KernelLog::Category::Core, "Core", "read EFI", "Apple EFI runtime already active; startup skipped");
        return false;
    }
    auto runtime = static_cast<EFI_RUNTIME_SERVICES_64 *>(gPEEFIRuntimeServices);
    if (!runtime || !runtime->GetVariable || !runtime->SetVariable) {
        KernelLog::logError(KernelLog::Category::Core, "Core", "read EFI", "EFI runtime services unavailable; startup skipped");
        return false;
    }
    auto services = EfiRuntimeServices::get(true);
    if (!services) {
        KernelLog::logError(KernelLog::Category::Core, "Core", "read EFI", "Lilu EFI runtime wrapper unavailable; startup skipped");
        return false;
    }
    EfiFlagState enabled = readEfiFlag(services, enableEfiKey);
    if (enabled == EfiFlagState::Enabled) {
        services->put();
        KernelLog::logInfo(KernelLog::Category::Core, "Core", "read EFI", "Persistent enable flag set");
        return true;
    }
    if (enabled == EfiFlagState::Invalid) {
        services->put();
        KernelLog::logError(KernelLog::Category::Core, "Core", "read EFI", "Enable flag unavailable or invalid; startup skipped");
        return false;
    }
    EfiFlagState test = readEfiFlag(services, testEfiKey);
    if (test != EfiFlagState::Missing) {
        services->put();
        KernelLog::logWarning(KernelLog::Category::Core, "Core", "read EFI", "Test flag present or unreadable; startup skipped");
        return false;
    }
    uint8_t used = 1;
    constexpr uint32_t attributes = EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS;
    uint64_t status = services->setVariable(testEfiKey, &bootGuid, attributes, sizeof(used), &used);
    EfiFlagState recorded = status == EFI_SUCCESS ? readEfiFlag(services, testEfiKey) : EfiFlagState::Invalid;
    services->put();
    if (recorded != EfiFlagState::Enabled) {
        KernelLog::logError(KernelLog::Category::Core, "Core", "write EFI", "Unable to consume and verify test flag; startup skipped");
        return false;
    }
    KernelLog::logInfo(KernelLog::Category::Core, "Core", "start test", "One-time test flag consumed through EFI runtime");
    return true;
}

bool isRembrandtHookEnabled() {
    auto options = IORegistryEntry::fromPath("/options", gIODTPlane);
    if (!options || !OSDynamicCast(IODTNVRAM, options)) {
        if (options) options->release();
        KernelLog::logInfo(KernelLog::Category::Core, "Core", "read NVRAM", "NVRAM controller not ready; checking EFI runtime");
        return authorizeEfiStartup();
    }
    auto enableObject = options->copyProperty(enableKey);
    auto enableData = OSDynamicCast(OSData, enableObject);
    if (enableObject && (!enableData || enableData->getLength() != 1 ||
        *static_cast<const uint8_t *>(enableData->getBytesNoCopy()) > 1)) {
        enableObject->release();
        options->release();
        KernelLog::logError(KernelLog::Category::Core, "Core", "read NVRAM", "Invalid enable flag; startup skipped");
        return false;
    }
    bool enabled = enableData && *static_cast<const uint8_t *>(enableData->getBytesNoCopy()) == 1;
    if (enableObject) enableObject->release();
    if (enabled) {
        options->release();
        KernelLog::logInfo(KernelLog::Category::Core, "Core", "read NVRAM", "Persistent enable flag set");
        return true;
    }
    auto testObject = options->copyProperty(testKey);
    if (testObject) {
        testObject->release();
        options->release();
        KernelLog::logInfo(KernelLog::Category::Core, "Core", "read NVRAM", "Test already consumed; startup skipped");
        return false;
    }
    const uint8_t testUsed = 1;
    auto data = OSData::withBytes(&testUsed, sizeof(testUsed));
    bool written = data && options->setProperty(testKey, data);
    if (data) data->release();
    if (!written) {
        options->release();
        KernelLog::logError(KernelLog::Category::Core, "Core", "write NVRAM", "Unable to consume test flag; startup skipped");
        return false;
    }
    auto sync = OSString::withCString("now");
    bool synced = sync && options->setProperty("IONVRAM-FORCESYNCNOW-PROPERTY", sync);
    if (sync) sync->release();
    if (!synced) {
        options->release();
        KernelLog::logError(KernelLog::Category::Core, "Core", "sync NVRAM", "Unable to synchronize test flag; startup skipped");
        return false;
    }
    auto value = options->copyProperty(testKey);
    auto valueData = OSDynamicCast(OSData, value);
    bool testRecorded = valueData && valueData->getLength() == 1 &&
        *static_cast<const uint8_t *>(valueData->getBytesNoCopy()) == testUsed;
    if (value) value->release();
    options->release();
    if (!testRecorded) {
        KernelLog::logError(KernelLog::Category::Core, "Core", "verify NVRAM", "Unable to verify test flag; startup skipped");
        return false;
    }
    KernelLog::logInfo(KernelLog::Category::Core, "Core", "start test", "One-time test flag consumed");
    return true;
}

void startRembrandtHook() {
    if (!isRembrandtHookEnabled()) {
        KernelLog::logInfo(KernelLog::Category::Core, "Core", "start", "Startup not authorized by NVRAM flags");
        return;
    }
    if (!KernelLog::startNvramLogging()) KernelLog::logWarning(KernelLog::Category::Core, "KernelLog", "start", "Unable to start NVRAM logging");
    KernelLog::logInfo(KernelLog::Category::Core, "Core", "start", "Initializing Rembrandt hooks");
    bool gpuReady = startGpuInterruptCleanup();
    bool usb4Ready = startUsb4PowerManagement();
    registerKextLoadHooks();
    KernelLog::logInfo(KernelLog::Category::Core, "Core", "start", "Registration complete: GPU cleanup=%u USB4 power=%u", static_cast<unsigned int>(gpuReady), static_cast<unsigned int>(usb4Ready));
}

}

PluginConfiguration ADDPR(config) {
    xStringify(PRODUCT_NAME),
    parseModuleVersion(xStringify(MODULE_VERSION)),
    LiluAPI::AllowNormal | LiluAPI::AllowSafeMode | LiluAPI::AllowInstallerRecovery,
    nullptr, 0,
    nullptr, 0,
    nullptr, 0,
    KernelVersion::BigSur,
    KernelVersion::Tahoe,
    startRembrandtHook
};

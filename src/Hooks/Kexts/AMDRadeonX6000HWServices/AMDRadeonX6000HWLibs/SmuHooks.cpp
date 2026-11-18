#include "Kexts/AMDRadeonX6000HWServices/AMDRadeonX6000HWLibs/HWLibsHooks.hpp"
#include "Hardware/Rembrandt/Smu13Mailbox.hpp"
#include "Hardware/Rembrandt/PciIdentity.hpp"
#include "Diagnostics/KernelLog.hpp"

extern void applyGcGolden();
extern void startGoldenGuard();
extern void dmubPollerStart();

namespace {

using SmuOperation = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
uint8_t smuContext[0x100];

template <size_t index>
uint64_t completeSmuOperation(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) {
    return 0;
}

const SmuOperation smuOperations[] {
    completeSmuOperation<0>, completeSmuOperation<1>, completeSmuOperation<2>, completeSmuOperation<3>,
    completeSmuOperation<4>, completeSmuOperation<5>, completeSmuOperation<6>, completeSmuOperation<7>,
    completeSmuOperation<8>, completeSmuOperation<9>, completeSmuOperation<10>, completeSmuOperation<11>,
    completeSmuOperation<12>, completeSmuOperation<13>, completeSmuOperation<14>, completeSmuOperation<15>,
    completeSmuOperation<16>, completeSmuOperation<17>, completeSmuOperation<18>, completeSmuOperation<19>,
    completeSmuOperation<20>,
};

uint64_t wrapInitializeSmuSoftware(uint64_t input, uint64_t output, uint64_t, uint64_t, uint64_t, uint64_t) {
    auto in = reinterpret_cast<uint32_t *>(input);
    auto out = reinterpret_cast<uint8_t *>(output);
    if (!in || !out || in[0] != 0x2c0 || *reinterpret_cast<uint32_t *>(out) != 0x50) {
        KernelLog::logError(KernelLog::Category::Hook, "SMU", "initialize software", "Invalid input or output structure");
        return 1;
    }
    *reinterpret_cast<void **>(out + 0x8) = smuContext;
    uint32_t argument = 0;
    if (!sendSmuMessage(0x02, argument)) KernelLog::logWarning(KernelLog::Category::Hook, "SMU", "initialize software", "Message 0x02 timed out");
    if (isRembrandt660M()) {
        KernelLog::logInfo(KernelLog::Category::Hook, "SMU", "initialize software", "660M: skipping startup graphics halt and MODE2 reset");
    } else {
        KernelLog::logInfo(KernelLog::Category::Hook, "SMU", "initialize software", "Preparing graphics engines using the existing reset path");
        haltGraphicsEngines();
        resetGraphicsEngines();
    }
    return 0;
}

uint64_t wrapInitializeSmuHardware(uint64_t handle, uint64_t input, uint64_t output, uint64_t, uint64_t, uint64_t) {
    if (!handle || !input || !output) {
        KernelLog::logError(KernelLog::Category::Hook, "SMU", "initialize hardware", "Missing initialization argument");
        return 1;
    }
    KernelLog::logInfo(KernelLog::Category::Hook, "SMU", "initialize hardware", "Applying graphics settings and starting display workers");
    uint32_t argument = 0;
    if (!sendSmuMessage(0x1a, argument)) KernelLog::logWarning(KernelLog::Category::Hook, "SMU", "initialize hardware", "Message 0x1a timed out");
    applyGcGolden();
    startGoldenGuard();
    dmubPollerStart();
    auto operations = reinterpret_cast<SmuOperation *>(output + 0x8);
    for (size_t i = 0; i < sizeof(smuOperations) / sizeof(smuOperations[0]); ++i) operations[i] = smuOperations[i];
    return 0;
}

uint64_t wrapExitSmu(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) {
    return 0;
}

}

void installSmuHooks(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
    KernelPatcher::RouteRequest requests[] {
        {"_smu_sw_init", wrapInitializeSmuSoftware},
        {"_smu_hw_init", wrapInitializeSmuHardware},
        {"_smu_hw_exit", wrapExitSmu},
        {"_smu_sw_exit", wrapExitSmu},
    };
    if (patcher.routeMultiple(index, requests, address, size)) KernelLog::logInfo(KernelLog::Category::Hook, "SMU", "install", "SMU hooks installed");
    else KernelLog::logError(KernelLog::Category::Hook, "SMU", "install", "SMU hooks failed: error=%d", static_cast<int>(patcher.getError()));
    patcher.clearError();
}

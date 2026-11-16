#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <libkern/c++/OSData.h>

namespace {

constexpr uint64_t registerApertureBytes = 0x80000;
volatile uint32_t *registers;
IOMemoryMap *registerMap;
IOLock *registerMapLock;

uint32_t getRegistryUint32(IORegistryEntry *entry, const char *key) {
    auto data = OSDynamicCast(OSData, entry->getProperty(key));
    return data && data->getLength() >= 4 ? *static_cast<const uint32_t *>(data->getBytesNoCopy()) : 0;
}

bool mapRegisterAperture() {
    if (__atomic_load_n(&registers, __ATOMIC_ACQUIRE)) return true;
    if (!registerMapLock) {
        auto lock = IOLockAlloc();
        if (lock && !OSCompareAndSwapPtr(nullptr, lock, &registerMapLock)) IOLockFree(lock);
    }
    if (!registerMapLock) return false;
    IOLockLock(registerMapLock);
    if (__atomic_load_n(&registers, __ATOMIC_ACQUIRE)) {
        IOLockUnlock(registerMapLock);
        return true;
    }
    auto matching = IOService::serviceMatching("IOPCIDevice");
    if (!matching) {
        IOLockUnlock(registerMapLock);
        return false;
    }
    auto iterator = IOService::getMatchingServices(matching);
    matching->release();
    if (!iterator) {
        IOLockUnlock(registerMapLock);
        return false;
    }
    while (auto service = OSDynamicCast(IOService, iterator->getNextObject())) {
        if (getRegistryUint32(service, "vendor-id") != 0x1002 || (getRegistryUint32(service, "class-code") >> 16) != 3) continue;
        for (unsigned i = 0; i < service->getDeviceMemoryCount(); ++i) {
            auto memory = service->getDeviceMemoryWithIndex(i);
            if (!memory || memory->getLength() != registerApertureBytes) continue;
            registerMap = memory->map(kIOMapInhibitCache);
            if (registerMap) __atomic_store_n(&registers,
                reinterpret_cast<volatile uint32_t *>(registerMap->getVirtualAddress()), __ATOMIC_RELEASE);
            break;
        }
        break;
    }
    iterator->release();
    bool available = __atomic_load_n(&registers, __ATOMIC_ACQUIRE) != nullptr;
    IOLockUnlock(registerMapLock);
    return available;
}

}

bool mmioAvailable() {
    return mapRegisterAperture();
}

uint32_t mmioRead(uint32_t reg) {
    return mapRegisterAperture() && reg < registerApertureBytes / 4 ? __atomic_load_n(&registers, __ATOMIC_ACQUIRE)[reg] : 0xffffffff;
}

void mmioWrite(uint32_t reg, uint32_t value) {
    if (mapRegisterAperture() && reg < registerApertureBytes / 4) __atomic_load_n(&registers, __ATOMIC_ACQUIRE)[reg] = value;
}

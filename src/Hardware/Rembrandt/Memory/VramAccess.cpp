#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <libkern/c++/OSData.h>

extern uint32_t mmioRead(uint32_t reg);
extern void mmioWrite(uint32_t reg, uint32_t value);

namespace {

IODeviceMemory *vramMemory;
IOMemoryMap *mappedPage;
uint64_t mappedPageOffset = ~0ull;
IOLock *vramLock;

uint32_t getRegistryUint32(IORegistryEntry *entry, const char *key) {
    auto data = OSDynamicCast(OSData, entry->getProperty(key));
    return data && data->getLength() >= 4 ? *static_cast<const uint32_t *>(data->getBytesNoCopy()) : 0;
}

IOLock *getVramLock() {
    if (!vramLock) {
        auto lock = IOLockAlloc();
        if (lock && !OSCompareAndSwapPtr(nullptr, lock, &vramLock)) IOLockFree(lock);
    }
    return vramLock;
}

bool findVramAperture() {
    if (vramMemory) return true;
    auto matching = IOService::serviceMatching("IOPCIDevice");
    auto iterator = matching ? IOService::getMatchingServices(matching) : nullptr;
    if (matching) matching->release();
    if (!iterator) return false;
    while (auto service = OSDynamicCast(IOService, iterator->getNextObject())) {
        if (getRegistryUint32(service, "vendor-id") != 0x1002 || (getRegistryUint32(service, "class-code") >> 16) != 3) continue;
        for (unsigned i = 0; i < service->getDeviceMemoryCount(); ++i) {
            auto memory = service->getDeviceMemoryWithIndex(i);
            if (memory && memory->getLength() >= 0x10000000) {
                memory->retain();
                vramMemory = memory;
                break;
            }
        }
        break;
    }
    iterator->release();
    return vramMemory != nullptr;
}

uint32_t readIndirectVram(uint64_t offset) {
    mmioWrite(0, static_cast<uint32_t>(offset) | 0x80000000u);
    mmioWrite(6, static_cast<uint32_t>(offset >> 31));
    return mmioRead(1);
}

volatile uint8_t *mapVramOffset(uint64_t offset) {
    uint64_t page = offset & ~0xfffull;
    if (page != mappedPageOffset) {
        if (mappedPage) mappedPage->release();
        mappedPage = vramMemory->createMappingInTask(kernel_task, 0, kIOMapAnywhere | kIOMapInhibitCache, page, 0x1000);
        mappedPageOffset = mappedPage ? page : ~0ull;
        if (!mappedPage) return nullptr;
    }
    return reinterpret_cast<volatile uint8_t *>(mappedPage->getVirtualAddress() + (offset & 0xfff));
}

}

uint64_t vramRead64(uint64_t offset) {
    auto lock = getVramLock();
    if (!lock) return ~0ull;
    IOLockLock(lock);
    if (!findVramAperture()) {
        IOLockUnlock(lock);
        return ~0ull;
    }
    uint64_t result;
    if (offset + 8 > vramMemory->getLength()) {
        result = readIndirectVram(offset) | static_cast<uint64_t>(readIndirectVram(offset + 4)) << 32;
    } else {
        auto pointer = mapVramOffset(offset);
        result = pointer ? *reinterpret_cast<volatile uint64_t *>(pointer) : ~0ull;
    }
    IOLockUnlock(lock);
    return result;
}

bool vramWrite32(uint64_t offset, uint32_t value) {
    auto lock = getVramLock();
    if (!lock) return false;
    IOLockLock(lock);
    if (!findVramAperture() || offset + 4 > vramMemory->getLength()) {
        IOLockUnlock(lock);
        return false;
    }
    auto pointer = mapVramOffset(offset);
    if (pointer) *reinterpret_cast<volatile uint32_t *>(pointer) = value;
    IOLockUnlock(lock);
    return pointer != nullptr;
}

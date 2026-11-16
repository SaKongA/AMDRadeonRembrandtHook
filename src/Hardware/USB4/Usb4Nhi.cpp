#include "Hardware/USB4/Usb4Nhi.hpp"
#include "Diagnostics/KernelLog.hpp"
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <libkern/OSByteOrder.h>

namespace {

constexpr uint32_t txRing = 0x00000, rxRing = 0x08000, txOptions = 0x19800, rxOptions = 0x29800;
constexpr uint32_t capabilities = 0x39640;
constexpr uint32_t ringEnable = 1u << 31, ringRaw = 1u << 30;
constexpr uint32_t descriptorCompleted = 0x2, descriptorPosted = 0x4, descriptorInterrupt = 0x8;
constexpr uint32_t packetRead = 1, packetWrite = 2, packetError = 3, switchSpace = 2;
constexpr uint32_t ringSize = 10, frameSize = 0x100;
constexpr uint32_t txDescriptorOffset = 0x000, rxDescriptorOffset = 0x100;
constexpr uint32_t txBufferOffset = 0x200, rxBufferOffset = 0x300;

struct RingDescriptor {
    uint64_t physicalAddress;
    uint32_t control;
    uint32_t time;
};

IOPCIDevice *nhiDevice;
IOPCIAddressSpace nhiAddress;
IOMemoryMap *nhiMap;
volatile uint8_t *nhiRegisters;
IOBufferMemoryDescriptor *dmaMemory;
uint8_t *dmaBytes;
uint64_t dmaPhysical;
uint32_t txHead, rxHead, rxTail, sequence;
IOLock *nhiLock;

uint32_t readNhiRegister(uint32_t offset) {
    return *reinterpret_cast<volatile uint32_t *>(nhiRegisters + offset);
}

void writeNhiRegister(uint32_t offset, uint32_t value) {
    *reinterpret_cast<volatile uint32_t *>(nhiRegisters + offset) = value;
}

RingDescriptor *getTxDescriptor(uint32_t index) {
    return reinterpret_cast<RingDescriptor *>(dmaBytes + txDescriptorOffset) + index;
}

RingDescriptor *getRxDescriptor(uint32_t index) {
    return reinterpret_cast<RingDescriptor *>(dmaBytes + rxDescriptorOffset) + index;
}

uint32_t calculateCrc32c(const uint8_t *data, size_t size) {
    uint32_t crc = ~0u;
    while (size--) {
        crc ^= *data++;
        for (uint32_t i = 0; i < 8; ++i) crc = (crc >> 1) ^ (0x82f63b78u & (0u - (crc & 1)));
    }
    return ~crc;
}

bool prepareNhi() {
    if (!nhiDevice) return false;
    if (!nhiMap) {
        auto memory = nhiDevice->getDeviceMemoryWithIndex(0);
        if (!memory || memory->getLength() < 0x40000) return false;
        nhiMap = memory->map(kIOMapInhibitCache);
        if (!nhiMap) return false;
        nhiRegisters = reinterpret_cast<volatile uint8_t *>(nhiMap->getVirtualAddress());
    }
    uint16_t command = nhiDevice->configRead16(nhiAddress, static_cast<UInt8>(kIOPCIConfigCommand));
    if ((command & 0x6) != 0x6)
        nhiDevice->configWrite16(nhiAddress, static_cast<UInt8>(kIOPCIConfigCommand), command | 0x6);
    for (uint8_t offset = nhiDevice->configRead8(nhiAddress, 0x34) & 0xfc, count = 0; offset && count < 48; ++count) {
        if (nhiDevice->configRead8(nhiAddress, offset) == 0x01) {
            uint16_t power = nhiDevice->configRead16(nhiAddress, offset + 4);
            if (power & 3) {
                nhiDevice->configWrite16(nhiAddress, offset + 4, power & ~3u);
                IOSleep(10);
            }
            break;
        }
        offset = nhiDevice->configRead8(nhiAddress, offset + 1) & 0xfc;
    }
    if (!dmaMemory) {
        auto memory = IOBufferMemoryDescriptor::withOptions(kIODirectionInOut | kIOMemoryPhysicallyContiguous, PAGE_SIZE, PAGE_SIZE);
        if (!memory) return false;
        if (memory->prepare() != kIOReturnSuccess) {
            memory->release();
            return false;
        }
        dmaMemory = memory;
        dmaBytes = static_cast<uint8_t *>(dmaMemory->getBytesNoCopy());
        dmaPhysical = dmaMemory->getPhysicalSegment(0, nullptr, kIOMemoryMapperNone);
    }
    return readNhiRegister(capabilities) != 0xffffffff && dmaBytes && dmaPhysical;
}

void stopNhiRings() {
    writeNhiRegister(txOptions, 0);
    writeNhiRegister(rxOptions, 0);
    for (uint32_t offset = 0; offset < 16; offset += 4) {
        writeNhiRegister(txRing + offset, 0);
        writeNhiRegister(rxRing + offset, 0);
    }
}

void postNhiReceive(uint32_t slot) {
    auto descriptor = getRxDescriptor(slot);
    descriptor->physicalAddress = dmaPhysical + rxBufferOffset + slot * frameSize;
    descriptor->time = 0;
    descriptor->control = (descriptorPosted | descriptorInterrupt) << 20;
}

void startNhiRings() {
    bzero(dmaBytes, rxBufferOffset + ringSize * frameSize);
    txHead = rxTail = 0;
    rxHead = ringSize - 1;
    for (uint32_t i = 0; i < rxHead; ++i) postNhiReceive(i);
    uint64_t tx = dmaPhysical + txDescriptorOffset, rx = dmaPhysical + rxDescriptorOffset;
    writeNhiRegister(txRing, static_cast<uint32_t>(tx));
    writeNhiRegister(txRing + 4, static_cast<uint32_t>(tx >> 32));
    writeNhiRegister(txRing + 12, ringSize);
    writeNhiRegister(txOptions + 4, 0);
    writeNhiRegister(txOptions, ringEnable | ringRaw);
    writeNhiRegister(rxRing, static_cast<uint32_t>(rx));
    writeNhiRegister(rxRing + 4, static_cast<uint32_t>(rx >> 32));
    writeNhiRegister(rxRing + 12, (frameSize << 16) | ringSize);
    writeNhiRegister(rxOptions + 4, 0xffffffff);
    writeNhiRegister(rxOptions, ringEnable | ringRaw);
    __sync_synchronize();
    writeNhiRegister(rxRing + 8, rxHead);
}

uint32_t transferNhiPacket(uint32_t type, const uint32_t *words, uint32_t count, uint32_t expected,
                           uint32_t *output, uint32_t capacity, uint32_t &outputCount) {
    auto buffer = reinterpret_cast<uint32_t *>(dmaBytes + txBufferOffset);
    for (uint32_t i = 0; i < count; ++i) buffer[i] = OSSwapHostToBigInt32(words[i]);
    buffer[count] = OSSwapHostToBigInt32(calculateCrc32c(reinterpret_cast<uint8_t *>(buffer), count * 4));
    auto descriptor = getTxDescriptor(txHead);
    descriptor->physicalAddress = dmaPhysical + txBufferOffset;
    descriptor->time = 0;
    descriptor->control = (count * 4 + 4) | (type << 12) | (type << 16) | ((descriptorPosted | descriptorInterrupt) << 20);
    __sync_synchronize();
    txHead = (txHead + 1) % ringSize;
    writeNhiRegister(txRing + 8, txHead << 16);
    for (uint32_t i = 0; i < 1000; ++i) {
        auto receive = getRxDescriptor(rxTail);
        uint32_t control = __atomic_load_n(&receive->control, __ATOMIC_ACQUIRE);
        if (!(control & (descriptorCompleted << 20))) {
            IODelay(100);
            continue;
        }
        uint32_t length = (control & 0xfff) ? control & 0xfff : 4096;
        uint32_t packet = (control >> 12) & 0xf;
        auto data = reinterpret_cast<uint32_t *>(dmaBytes + rxBufferOffset + rxTail * frameSize);
        bool valid = length >= 8 && length <= frameSize && !(length & 3);
        uint32_t crc = valid ? OSSwapBigToHostInt32(data[length / 4 - 1]) : 0;
        valid = valid && crc == calculateCrc32c(reinterpret_cast<uint8_t *>(data), length - 4);
        outputCount = 0;
        if (valid)
            for (; outputCount < length / 4 - 1 && outputCount < capacity; ++outputCount)
                output[outputCount] = OSSwapBigToHostInt32(data[outputCount]);
        postNhiReceive(rxHead);
        rxHead = (rxHead + 1) % ringSize;
        rxTail = (rxTail + 1) % ringSize;
        __sync_synchronize();
        writeNhiRegister(rxRing + 8, rxHead);
        if (valid && (packet == expected || packet == packetError)) return packet;
    }
    return 0;
}

uint32_t makeRouterConfigAddress(uint32_t offset, uint32_t count) {
    sequence = (sequence + 1) & 3;
    return offset | (count << 13) | (switchSpace << 25) | (sequence << 27);
}

bool readRouterConfig(uint32_t offset, uint32_t count, uint32_t *values) {
    uint32_t request[] {0, 0, makeRouterConfigAddress(offset, count)}, reply[19];
    uint32_t received = 0;
    uint32_t result = transferNhiPacket(packetRead, request, 3, packetRead, reply, 19, received);
    if (result != packetRead || received < 3 + count || (reply[2] & 0x0607ffff) != (request[2] & 0x0607ffff)) return false;
    for (uint32_t i = 0; i < count; ++i) values[i] = reply[3 + i];
    return true;
}

bool writeRouterConfig(uint32_t offset, uint32_t value) {
    uint32_t request[] {0, 0, makeRouterConfigAddress(offset, 1), value}, reply[4];
    uint32_t received = 0;
    return transferNhiPacket(packetWrite, request, 4, packetWrite, reply, 4, received) == packetWrite;
}

}

bool initializeUsb4Nhi() {
    if (!nhiLock) nhiLock = IOLockAlloc();
    return nhiLock != nullptr;
}

bool attachUsb4Nhi(IOPCIDevice *device, IOPCIAddressSpace address) {
    if (!nhiLock || !device) return false;
    IOLockLock(nhiLock);
    bool attached = !nhiDevice;
    if (attached) {
        device->retain();
        nhiDevice = device;
        nhiAddress = address;
    }
    IOLockUnlock(nhiLock);
    return attached;
}

bool runUsb4Router(bool sleep) {
    if (!nhiLock) return false;
    IOLockLock(nhiLock);
    bool success = false;
    bool prepared = false;
    if (prepareNhi()) {
        prepared = true;
        stopNhiRings();
        startNhiRings();
        uint32_t registers[8];
        success = readRouterConfig(0, 8, registers);
        if (success && sleep) {
            success = writeRouterConfig(5, registers[5] | 1u);
            if (success) {
                success = false;
                for (uint32_t milliseconds = 0; milliseconds <= 500; milliseconds += 10) {
                    uint32_t status = 0;
                    if (!readRouterConfig(6, 1, &status)) break;
                    if (status & 1) { success = true; break; }
                    IOSleep(10);
                }
            }
        }
        stopNhiRings();
    }
    IOLockUnlock(nhiLock);
    if (!prepared) KernelLog::logWarning(KernelLog::Category::Hardware, "USB4NHI", "prepare", "NHI mapping, DMA or controller readiness failed");
    else if (!success) KernelLog::logWarning(KernelLog::Category::Hardware, "USB4NHI", "transfer", "Router %s operation failed", sleep ? "sleep" : "probe");
    return success;
}

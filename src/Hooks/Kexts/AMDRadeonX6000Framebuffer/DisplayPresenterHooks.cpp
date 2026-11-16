#include "Kexts/AMDRadeonX6000Framebuffer/FramebufferHooks.hpp"
#include "Diagnostics/KernelLog.hpp"
#include <IOKit/IOLib.h>
#include <libkern/c++/OSObject.h>
#include <kern/thread.h>

namespace {

constexpr uint32_t presenterCount = 6;
mach_vm_address_t originalVblank;
mach_vm_address_t originalVupdate;
mach_vm_address_t originalEnqueue;
mach_vm_address_t originalEnable;
mach_vm_address_t setPipeInterruptState;
const OSMetaClass *presenterMetaClass;
struct PresenterState {
    uint64_t presenter;
    uint32_t vblankCount;
    uint32_t vupdateCount;
    uint32_t enqueueCount;
};
PresenterState presenters[presenterCount];
bool presenterEnabled[presenterCount];
IOSimpleLock *presenterLock;
bool presenterTracking;

uint64_t readPresenterField(uint64_t presenter, uint32_t offset) {
    return *reinterpret_cast<volatile uint64_t *>(presenter + offset);
}

int32_t recordPresenter(uint64_t presenter) {
    if (!presenter || !presenterMetaClass || reinterpret_cast<OSObject *>(presenter)->getMetaClass() != presenterMetaClass) return -1;
    uint8_t index = *reinterpret_cast<volatile uint8_t *>(presenter + 0xa0);
    if (index >= presenterCount) return -1;
    OSObject *previous = nullptr;
    auto interruptState = IOSimpleLockLockDisableInterrupt(presenterLock);
    if (!presenterTracking) {
        IOSimpleLockUnlockEnableInterrupt(presenterLock, interruptState);
        return -1;
    }
    if (presenters[index].presenter != presenter) {
        reinterpret_cast<OSObject *>(presenter)->retain();
        previous = reinterpret_cast<OSObject *>(presenters[index].presenter);
        presenters[index] = {presenter, 0, 0, 0};
        presenterEnabled[index] = false;
    }
    IOSimpleLockUnlockEnableInterrupt(presenterLock, interruptState);
    if (previous) previous->release();
    return index;
}

void incrementPresenterCount(int32_t index, uint64_t presenter, uint32_t PresenterState::*field) {
    if (index < 0) return;
    auto interruptState = IOSimpleLockLockDisableInterrupt(presenterLock);
    if (presenters[index].presenter == presenter) ++(presenters[index].*field);
    IOSimpleLockUnlockEnableInterrupt(presenterLock, interruptState);
}

void wrapVblank(uint64_t owner, uint64_t argument) {
    auto index = recordPresenter(owner);
    reinterpret_cast<void (*)(uint64_t, uint64_t)>(originalVblank)(owner, argument);
    incrementPresenterCount(index, owner, &PresenterState::vblankCount);
}

void wrapVupdate(uint64_t owner, uint64_t argument) {
    auto index = recordPresenter(owner);
    reinterpret_cast<void (*)(uint64_t, uint64_t)>(originalVupdate)(owner, argument);
    incrementPresenterCount(index, owner, &PresenterState::vupdateCount);
}

uint64_t wrapEnqueue(uint64_t presenter, uint64_t mask) {
    auto index = recordPresenter(presenter);
    uint64_t result = reinterpret_cast<uint64_t (*)(uint64_t, uint64_t)>(originalEnqueue)(presenter, mask);
    incrementPresenterCount(index, presenter, &PresenterState::enqueueCount);
    return result;
}

uint32_t wrapEnable(uint64_t presenter) {
    return reinterpret_cast<uint32_t (*)(uint64_t)>(originalEnable)(presenter);
}

void stopPresenterTracking() {
    OSObject *objects[presenterCount] {};
    auto interruptState = IOSimpleLockLockDisableInterrupt(presenterLock);
    presenterTracking = false;
    for (uint32_t i = 0; i < presenterCount; ++i) {
        objects[i] = reinterpret_cast<OSObject *>(presenters[i].presenter);
        presenters[i].presenter = 0;
    }
    IOSimpleLockUnlockEnableInterrupt(presenterLock, interruptState);
    for (auto object : objects) if (object) object->release();
}

void watchPresenters(void *, wait_result_t) {
    uint32_t quiet[presenterCount] {};
    uint64_t observed[presenterCount] {};
    PresenterState previous[presenterCount] {};
    uint32_t progress = 0;
    while (progress < 3000) {
        IOSleep(5000);
        ++progress;
        for (uint32_t i = 0; i < presenterCount; ++i) {
            auto interruptState = IOSimpleLockLockDisableInterrupt(presenterLock);
            uint64_t presenter = presenters[i].presenter;
            bool enabled = presenterEnabled[i];
            if (presenter) reinterpret_cast<OSObject *>(presenter)->retain();
            IOSimpleLockUnlockEnableInterrupt(presenterLock, interruptState);
            if (!presenter) continue;
            if (observed[i] != presenter) {
                observed[i] = presenter;
                quiet[i] = 0;
            }
            if (enabled || !originalEnable) {
                reinterpret_cast<OSObject *>(presenter)->release();
                continue;
            }
            if (!readPresenterField(presenter, 0xd8) || readPresenterField(presenter, 0x158)) {
                quiet[i] = 0;
                reinterpret_cast<OSObject *>(presenter)->release();
                continue;
            }
            if (++quiet[i] < 3) {
                reinterpret_cast<OSObject *>(presenter)->release();
                continue;
            }
            interruptState = IOSimpleLockLockDisableInterrupt(presenterLock);
            bool current = presenters[i].presenter == presenter && !presenterEnabled[i];
            if (current) presenterEnabled[i] = true;
            IOSimpleLockUnlockEnableInterrupt(presenterLock, interruptState);
            if (!current) {
                reinterpret_cast<OSObject *>(presenter)->release();
                continue;
            }
            uint64_t framebuffer = readPresenterField(presenter, 0x1650);
            uint32_t result = reinterpret_cast<uint32_t (*)(uint64_t)>(originalEnable)(presenter);
            if (!result && framebuffer && setPipeInterruptState)
                reinterpret_cast<uint32_t (*)(uint64_t, bool)>(setPipeInterruptState)(framebuffer, true);
            reinterpret_cast<OSObject *>(presenter)->release();
        }
        for (uint32_t i = 0; i < presenterCount; ++i) {
            auto interruptState = IOSimpleLockLockDisableInterrupt(presenterLock);
            PresenterState state = presenters[i];
            IOSimpleLockUnlockEnableInterrupt(presenterLock, interruptState);
            if (!state.presenter || (state.vblankCount == previous[i].vblankCount &&
                state.vupdateCount == previous[i].vupdateCount && state.enqueueCount == previous[i].enqueueCount)) continue;
            previous[i] = state;
            ++progress;
        }
    }
    stopPresenterTracking();
}

}

void installDisplayPresenterHooks(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
    if (!presenterLock) presenterLock = IOSimpleLockAlloc();
    if (!presenterLock) {
        KernelLog::logError(KernelLog::Category::Hook, "Presenter", "initialize", "Unable to allocate tracking lock");
        return;
    }
    auto interruptState = IOSimpleLockLockDisableInterrupt(presenterLock);
    presenterTracking = true;
    IOSimpleLockUnlockEnableInterrupt(presenterLock, interruptState);
    presenterMetaClass = reinterpret_cast<const OSMetaClass *>(
        patcher.solveSymbol(index, "__ZN29AMDRadeonX6000_AmdFbPresenter10gMetaClassE", address, size));
    if (!presenterMetaClass) KernelLog::logWarning(KernelLog::Category::Hook, "Presenter", "resolve", "Presenter metaclass unavailable: error=%d", static_cast<int>(patcher.getError()));
    patcher.clearError();
    setPipeInterruptState = patcher.solveSymbol(index,
        "__ZN35AMDRadeonX6000_AmdRadeonFramebuffer21setPipeInterruptStateEb", address, size);
    if (!setPipeInterruptState) KernelLog::logWarning(KernelLog::Category::Hook, "Presenter", "resolve", "Pipe interrupt function unavailable: error=%d", static_cast<int>(patcher.getError()));
    patcher.clearError();
    KernelPatcher::RouteRequest requests[] {
        {"__ZN29AMDRadeonX6000_AmdFbPresenter24transactionVBlankHandlerEP8OSObjectPv", wrapVblank, originalVblank},
        {"__ZN29AMDRadeonX6000_AmdFbPresenter25transactionVUpdateHandlerEP8OSObjectPv", wrapVupdate, originalVupdate},
        {"__ZN29AMDRadeonX6000_AmdFbPresenter19enqueueTransactionsEy", wrapEnqueue, originalEnqueue},
        {"__ZN29AMDRadeonX6000_AmdFbPresenter18enableTransactionsEv", wrapEnable, originalEnable},
    };
    if (patcher.routeMultiple(index, requests, address, size)) {
        KernelLog::logInfo(KernelLog::Category::Hook, "Presenter", "install", "Presenter hooks installed");
        thread_t thread;
        if (kernel_thread_start(watchPresenters, nullptr, &thread) == KERN_SUCCESS) thread_deallocate(thread);
        else {
            KernelLog::logError(KernelLog::Category::Hook, "Presenter", "start", "Unable to start presenter watch");
            stopPresenterTracking();
        }
    } else {
        KernelLog::logError(KernelLog::Category::Hook, "Presenter", "install", "Presenter hooks failed: error=%d", static_cast<int>(patcher.getError()));
        stopPresenterTracking();
    }
    patcher.clearError();
}

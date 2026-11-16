#include <stdint.h>
#include "Diagnostics/Diagnostics.hpp"

extern uint32_t mmioRead(uint32_t reg);
extern void mmioWrite(uint32_t reg, uint32_t value);

void quiesceIh(const char *reason) {
    constexpr uint32_t base = 0x10a0;
    constexpr uint32_t rings[][3] {{0x80, 0x83, 0x84}, {0x8c, 0x8f, 0x90}, {0x98, 0x9b, 0x9c}};
    uint32_t controls[3] {};
    for (uint32_t i = 0; i < 3; ++i) {
        uint32_t control = mmioRead(base + rings[i][0]);
        if (control == 0xffffffff) {
            logInterruptDiagnostics(reason, controls, i);
            return;
        }
        controls[i] = control;
        if (!i) control &= ~(1u << 17);
        mmioWrite(base + rings[i][0], control & ~1u);
        mmioWrite(base + rings[i][1], 0);
        mmioWrite(base + rings[i][2], 0);
    }
    logInterruptDiagnostics(reason, controls, 3);
}

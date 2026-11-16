#pragma once

#include <stdint.h>

struct DisplayStream {
    uint32_t pixelClockKhz;
    uint32_t encoding;
    uint32_t depth;
    uint32_t link;
};

const DisplayStream &getExternalDisplayStream();
void setExternalDisplayStream(uint32_t pixelClockKhz, uint32_t encoding, uint32_t depth, uint32_t link);
void configureHdmiPixelClock(uint64_t ring, uint32_t offset, uint32_t clock);

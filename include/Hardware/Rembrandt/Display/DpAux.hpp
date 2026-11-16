#pragma once

#include <stdint.h>

bool startPanelLinkWatch();
void setPanelLinkWatchPaused(bool paused);
void requestPanelLinkRetraining();
void recordDalAuxAccess(uint32_t reg);

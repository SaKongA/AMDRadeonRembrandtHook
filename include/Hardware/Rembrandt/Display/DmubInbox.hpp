#pragma once

#include <IOKit/IOLocks.h>
#include <stdint.h>

extern IOLock *gDmubInboxLock;
extern uint32_t gDalWritePointer;
extern uint32_t gDmubAhead;

bool initializeDmubInbox();
bool isDmubInboxHeld();
uint64_t getDmubInboxOffset();
void resetDmubInbox();
bool dmubInjectCmd(const uint32_t *command, uint32_t *reply);
uint32_t realignDmubInbox(uint32_t writePointer);

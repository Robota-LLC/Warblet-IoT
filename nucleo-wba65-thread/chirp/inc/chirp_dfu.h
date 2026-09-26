/* chirp_dfu.h — hand the CPU to the ROM bootloader at 0x0BF90000. */
#ifndef CHIRP_DFU_H
#define CHIRP_DFU_H

#include "openthread/instance.h"

/* Stop Thread and enter ROM DFU. Never returns. Pass NULL if no instance exists. */
void chirp_dfu_enter(otInstance *inst);

#endif /* CHIRP_DFU_H */

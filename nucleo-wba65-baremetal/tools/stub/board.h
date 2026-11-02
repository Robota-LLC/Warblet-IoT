/* The board, for the host test only (tools/valtest.c): the one thing the
 * setup code reads from it is the die's unique id. */
#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>

extern unsigned test_uid;                   /* defined by the test */
#define UID_BASE ((uintptr_t)&test_uid)

#endif /* BOARD_H */

#ifndef DFU_H
#define DFU_H

/* Never returns: hands the CPU to the ROM bootloader at 0x0BF90000. */
void dfu_enter_system_bootloader(void);

#endif /* DFU_H */

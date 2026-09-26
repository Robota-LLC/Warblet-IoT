/* ST-LINK serial adapter: DMA reception, line assembly, and dispatch.
 * Send CHIRP lines to the provisioning callback and other lines to OpenThread CLI.
 * Reject damaged lines instead of dispatching partial commands. */
#ifndef BOARD_SERIAL_H
#define BOARD_SERIAL_H

/* CHIRP-PROV allows a 4096-byte push; 64 bytes of margin so a line at the
 * limit is never over it. */
#define BOARD_SERIAL_LINE_MAX 4160u

/* printf-style, into the same sink as ot-cli so lines interleave in order. */
void board_serial_print(const char *fmt, ...);

/* Flush queued output and let it leave the wire; call before the jump to the
 * ROM bootloader. */
void board_serial_drain(void);

/* Take reception over from ST's per-byte path. `on_chirp_line` gets each
 * complete "CHIRP" line from the scheduler, never an interrupt, in a writable
 * NUL-terminated buffer. Other lines go to the OpenThread CLI. */
void board_serial_begin(void (*on_chirp_line)(char *line, unsigned len));

#endif /* BOARD_SERIAL_H */

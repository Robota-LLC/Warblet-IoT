/* ST-LINK serial adapter: DMA reception, line assembly, and dispatch.
 * Send CHIRP lines to the provisioning callback and other lines to OpenThread CLI.
 * Drop damaged lines instead of dispatching partial commands. */
#ifndef BOARD_SERIAL_H
#define BOARD_SERIAL_H

/* The longest line, counted from its first byte: a setup line starts with
 * "CHIRP", so CHIRP-PROV's 4096 counts from there. A longer line is thrown
 * away whole, through its line ending, and gets no answer. */
#define BOARD_SERIAL_LINE_MAX 4096u

/* A setup line whose bytes stop for this long is given up, wherever it had
 * got to: longer than the 1 s a browser tab out of view waits between the
 * pieces of a push. What follows, to the next line ending, is the rest of it
 * and is dropped, unless it starts with CHIRP. A CLI line is typed by a
 * person and waits as long as it takes. */
#define BOARD_SERIAL_IDLE_MS 1500u

/* printf-style, into the same sink as ot-cli so lines interleave in order. */
void board_serial_print(const char *fmt, ...);

/* Flush queued output and let it leave the wire; call before the jump to the
 * ROM bootloader. */
void board_serial_drain(void);

/* Take reception over from ST's per-byte path. `on_chirp_line` gets each
 * complete "CHIRP" line from the scheduler, never an interrupt, in a writable
 * NUL-terminated buffer. Other lines go to the OpenThread CLI, except the rest
 * of a setup line given up on a pause and a line holding "CHIRP+" further in,
 * which are dropped unechoed: a push can hold secrets. */
void board_serial_begin(void (*on_chirp_line)(char *line, unsigned len));

#endif /* BOARD_SERIAL_H */

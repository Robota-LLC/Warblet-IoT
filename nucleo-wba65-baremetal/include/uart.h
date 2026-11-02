#ifndef UART_H
#define UART_H

/* A line whose bytes stop for this long is given up: longer than the 1 s a
 * browser tab out of view waits between the pieces of a push (CHIRP-PROV). */
#define UART_IDLE_MS 1500u

/* uart_getc()'s flags, above the byte's 8 bits. */
#define UART_RX_PAUSED 0x100   /* the line had been quiet for UART_IDLE_MS before it */
#define UART_RX_LOST   0x200   /* bytes were lost just before it: a UART error or a full ring */

void uart_init(void);
void uart_putc(char c);
void uart_write(const char *s, unsigned n);
void uart_puts(const char *s);
void uart_line(const char *s);   /* puts + CRLF */
void uart_put_uint(unsigned v);  /* decimal */
void uart_put_hex(unsigned v, int digits);  /* lowercase, zero-padded */
int  uart_getc(void);            /* -1 when no byte is waiting; else the byte and its flags */
void uart_flush(void);           /* block until the last byte has left the shifter */

#endif /* UART_H */

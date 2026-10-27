#ifndef UART_H
#define UART_H

void uart_init(void);
void uart_putc(char c);
void uart_write(const char *s, unsigned n);
void uart_puts(const char *s);
void uart_line(const char *s);   /* puts + CRLF */
void uart_put_uint(unsigned v);  /* decimal */
void uart_put_hex(unsigned v, int digits);  /* lowercase, zero-padded */
int  uart_getc(void);            /* -1 when no byte is waiting */
void uart_flush(void);           /* block until the last byte has left the shifter */

#endif /* UART_H */

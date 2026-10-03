#ifndef UART_H
#define UART_H

void uart_init(void);
void uart_putc(char c);
void uart_write(const char *s, unsigned n);
void uart_puts(const char *s);
void uart_line(const char *s);   /* puts + CRLF */
int  uart_getc(void);            /* -1 when no byte is waiting */
void uart_flush(void);           /* block until the last byte has left the shifter */

#endif /* UART_H */

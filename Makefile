CC = gcc
CFLAGS = -Wall -Werror -lftd2xx -lftdi1 -pthread -std=gnu99 -D_FILE_OFFSET_BITS=64

all: set_fifo_mode set_uart_mode sample_grabber

set_fifo_mode : set_fifo_mode.c Makefile
	$(CC) set_fifo_mode.c -o set_fifo_mode $(CFLAGS)

set_uart_mode : set_uart_mode.c Makefile
	$(CC) set_uart_mode.c -o set_uart_mode $(CFLAGS)

sample_grabber : sample_grabber.c Makefile
	$(CC) sample_grabber.c -o sample_grabber pipe/pipe.c $(CFLAGS)

clean:
	rm set_fifo_mode
	rm set_uart_mode
	rm sample_grabber

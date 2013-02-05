CC = gcc
CFLAGS = -Wall -Werror -lftd2xx -lftdi1 -pthread -std=gnu99 -D_FILE_OFFSET_BITS=64

all: set_fifo_mode set_uart_mode stream_test

set_fifo_mode : set_fifo_mode.c Makefile
	$(CC) set_fifo_mode.c -o set_fifo_mode $(CFLAGS)

set_uart_mode : set_uart_mode.c Makefile
	$(CC) set_uart_mode.c -o set_uart_mode $(CFLAGS)

stream_test : stream_test.c Makefile
	$(CC) stream_test.c -o stream_test pipe/pipe.c $(CFLAGS)

clean:
	rm -f *.o
	rm set_fifo_mode
	rm set_uart_mode
	rm stream_test

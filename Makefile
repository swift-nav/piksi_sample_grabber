CC = gcc
CFLAGS = -Wall -Werror -std=gnu99 -Iinclude `pkg-config libusb-1.0 --cflags`
LDLIBS = `pkg-config libusb-1.0 --libs`

all: set_fifo_mode set_uart_mode sample_grabber

set_fifo_mode : set_fifo_mode.c libusb_hacks.c Makefile
	$(CC) set_fifo_mode.c libusb_hacks.c -o set_fifo_mode -lftd2xx $(CFLAGS) $(LDLIBS)

set_uart_mode : set_uart_mode.c libusb_hacks.c Makefile
	$(CC) set_uart_mode.c libusb_hacks.c -o set_uart_mode -lftd2xx $(CFLAGS) $(LDLIBS)

sample_grabber : sample_grabber.c Makefile
	$(CC) sample_grabber.c -o sample_grabber pipe/pipe.c -lftdi1 \
        -pthread -D_FILE_OFFSET_BITS=64 $(CFLAGS) $(LDLIBS)

clean:
	rm -f set_fifo_mode
	rm -f set_uart_mode
	rm -f sample_grabber

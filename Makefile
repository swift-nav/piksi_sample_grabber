CC = gcc
CFLAGS = -g -Wall -std=gnu99 -Iinclude `pkg-config libusb-1.0 --cflags`
LDLIBS = `pkg-config libusb-1.0 --libs`

all: set_fifo_mode set_uart_mode sample_grabber pack8 piksi_to_1bit

set_fifo_mode : set_fifo_mode.c libusb_hacks.c Makefile
	$(CC) set_fifo_mode.c libusb_hacks.c -o set_fifo_mode -lftd2xx $(CFLAGS) $(LDLIBS)

set_uart_mode : set_uart_mode.c libusb_hacks.c Makefile
	$(CC) set_uart_mode.c libusb_hacks.c -o set_uart_mode -lftd2xx $(CFLAGS) $(LDLIBS)

sample_grabber : sample_grabber.c Makefile
	$(CC) sample_grabber.c -o sample_grabber pipe/pipe.c -lftdi1 \
        -pthread -D_FILE_OFFSET_BITS=64 $(CFLAGS) $(LDLIBS)

pack8 : pack8.c Makefile
	$(CC) $< -o $@ $(CFLAGS)

piksi_to_1bit : piksi_to_1bit.c Makefile
	$(CC) $< -o $@ $(CFLAGS)

clean:
	rm -f set_fifo_mode
	rm -f set_uart_mode
	rm -f sample_grabber
	rm -f pack8
	rm -f piksi_to_1bit


CC = gcc
CFLAGS = -Wall -Werror -lftd2xx -std=gnu99

all: sample_grabber back_to_uart

sample_grabber : sample_grabber.c
	$(CC) sample_grabber.c -o sample_grabber $(CFLAGS)

back_to_uart : back_to_uart.c
	$(CC) back_to_uart.c -o back_to_uart $(CFLAGS)

clean:
	rm -f *.o
	rm sample_grabber
	rm back_to_uart


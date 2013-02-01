
CC = gcc
CFLAGS = -Wall -Werror -lftd2xx -lftdi1 -std=gnu99

all: sample_grabber back_to_uart stream_test

sample_grabber : sample_grabber.c Makefile
	$(CC) sample_grabber.c -o sample_grabber $(CFLAGS)

back_to_uart : back_to_uart.c Makefile
	$(CC) back_to_uart.c -o back_to_uart $(CFLAGS)

stream_test : stream_test.c Makefile
	$(CC) stream_test.c -o stream_test $(CFLAGS)

clean:
	rm -f *.o
	rm sample_grabber
	rm back_to_uart
	rm stream_test

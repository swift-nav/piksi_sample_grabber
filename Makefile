
CC = gcc
CFLAGS = -Wall -Werror -lftd2xx -std=gnu99

APP = sample_grabber

all: $(APP)

$(APP): sample_grabber.c
	$(CC) sample_grabber.c -o $(APP) $(CFLAGS)

clean:
	rm -f *.o
	rm $(APP)


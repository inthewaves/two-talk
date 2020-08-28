
CFLAGS = -Wall -Werror -std=c11 -D _POSIX_C_SOURCE=200809L -pthread

all: two-chat

two-chat: two-chat.o common.o message_sender.o message_listener.o keyboard_reader.o screen_printer.o list.o
	gcc $(CFLAGS) -o $@ two-chat.o common.o message_sender.o message_listener.o keyboard_reader.o \
	    screen_printer.o list.o

two-chat.o: two-chat.c
	gcc $(CFLAGS) -c two-chat.c

common.o: common.c common.h
	gcc $(CFLAGS) -c common.c

keyboard_reader.o: keyboard_reader.c keyboard_reader.h
	gcc $(CFLAGS) -c keyboard_reader.c

screen_printer.o: screen_printer.c screen_printer.h
	gcc $(CFLAGS) -c screen_printer.c

message_sender.o: message_sender.c message_sender.h
	gcc $(CFLAGS) -c message_sender.c

message_listener.o: message_listener.c message_listener.h
	gcc $(CFLAGS) -c message_listener.c

clean:
	mv list.o list.o.bak
	rm -f two-chat *.o
	mv list.o.bak list.o

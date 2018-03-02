CC=gcc
CFLAGS=-g -std=c89
OBJS=http.o example.o
NAME=example

.PHONY: $(NAME)

all: $(NAME)

$(NAME): $(OBJS)
	$(CC) -o $(NAME) $(OBJS) $(CFLAGS)

%.o: %.c
	$(CC) -c $< $(CFLAGS)


clean:
	rm -fr *.o $(NAME)


http.c: http.h
example.c: http.c http.h

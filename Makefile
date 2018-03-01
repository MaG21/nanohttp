CC=gcc
CFLAGS=-g -std=c89
OBJS=chunk.o header.o http.o example.o
NAME=example

.PHONY: $(NAME)

all: $(NAME)

$(NAME): $(OBJS)
	$(CC) -o $(NAME) $(OBJS) $(CFLAGS)

%.o: %.c
	$(CC) -c $< $(CFLAGS)


clean:
	rm -fr *.o $(NAME)


http.c: header.c header.h chunk.c chunk.c http.h
header.c: header.h
chunk.c: chunk.h

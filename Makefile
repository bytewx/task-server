CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -O2 -g -D_POSIX_C_SOURCE=200809L
LDFLAGS = -lpthread -lm
 
TARGET  = taskserver
SRCS    = main.c queue.c worker.c store.c
OBJS    = $(SRCS:.c=.o)
 
.PHONY: all clean run
 
all: $(TARGET)
 
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
 
%.o: %.c taskserver.h
	$(CC) $(CFLAGS) -c -o $@ $<
 
run: $(TARGET)
	./$(TARGET)
 
clean:
	rm -f $(OBJS) $(TARGET)

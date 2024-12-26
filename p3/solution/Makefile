CC = gcc
CFLAGS-common = -Wall -Wextra -Werror -pedantic -std=gnu18
CFLAGS = $(CFLAGS-common) -O2
CFLAGS-dbg = $(CFLAGS-common) -Og -ggdb
CFLAGS-asan = $(CFLAGS-common) -O1 -fsanitize=address
LOGIN = sganesh
SUBMITPATH = ~cs537-1/handin/${LOGIN}
TARGET = wsh

.PHONY: all clean submit test
all: $(TARGET) $(TARGET)-dbg $(TARGET)-asan

$(TARGET): $(TARGET).c $(TARGET).h
	$(CC) $(CFLAGS) $< -o $@

$(TARGET)-dbg: $(TARGET).c $(TARGET).h
	$(CC) $(CFLAGS-dbg) $< -o $@

$(TARGET)-asan: $(TARGET).c $(TARGET).h
	$(CC) $(CFLAGS-asan) $< -o $@

clean:
	rm -f $(TARGET) $(TARGET)-dbg $(TARGET)-asan

submit:
	cp -r ../../p3 $(SUBMITPATH)

test: all
	cd ../tests && ./run-tests.sh

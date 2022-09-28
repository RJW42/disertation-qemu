.PHONY: clean
CC = ~/Documents/Documents/zig-linux-x86_64/zig cc -static -target aarch64-linux-gnu
programs_srcs = $(wildcard benchmark-*.c)
programs = $(programs_srcs:.c=)

all: $(programs)

%: %.c
	$(CC) -o $@ $<

clean: 
	rm -f $(programs)
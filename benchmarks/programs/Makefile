.PHONY: clean
CC = ~/Documents/Documents/zig-linux-x86_64/zig cc -static -target aarch64-linux-gnu
programs = benchmark-basic benchmark-basic2

all: $(programs)

$(programs): %: %.c
	$(CC) -o $@ $<

clean: 
	rm -f $(programs)
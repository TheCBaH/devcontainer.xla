
all: build

build:hlo_test

hlo_test: hlo_test.c
	cc -g -W -Wall -o $@ $<

run: hlo_test
	$(if ${WITH_GDB},gdb) ./$<

clean:
	rm -f hlo_test

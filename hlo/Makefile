
all: build

build:hlo_testa

hlo_test: hlo_test.c
	cc -g -W -Wall -o $@ $<

run: hlo_test
	$(if ${WITH_GDB},gdb) ./$<

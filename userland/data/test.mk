# GNU make in the OS: make -f /data/test.mk (the recipes run with /bin/sh, which is dash)
all: /tmp/mk_hello
	-/tmp/mk_hello
	@echo "make built and ran a program with tcc"

/tmp/mk_hello: /data/tcc_hello.c
	/bin/tcc -static -o $@ $<

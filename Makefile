CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -Isrc/proto

all: bin/test_crc16 bin/test_decode bin/test_encode bin/test_escape bin/test_frame

bin/test_crc16: src/proto/crc16.c src/proto/test_crc16.c
	$(CC) $(CFLAGS) src/proto/crc16.c src/proto/test_crc16.c -o $@

bin/test_decode: src/proto/codec.c src/proto/crc16.c src/proto/escape.c src/proto/test_decode.c
	$(CC) $(CFLAGS) src/proto/codec.c src/proto/crc16.c src/proto/escape.c src/proto/test_decode.c -o $@

bin/test_encode: src/proto/codec.c src/proto/crc16.c src/proto/escape.c src/proto/test_encode.c
	$(CC) $(CFLAGS) src/proto/codec.c src/proto/crc16.c src/proto/escape.c src/proto/test_encode.c -o $@

bin/test_escape: src/proto/escape.c src/proto/test_escape.c
	$(CC) $(CFLAGS) src/proto/escape.c src/proto/test_escape.c -o $@

bin/test_frame: src/proto/frame.c src/proto/codec.c src/proto/crc16.c src/proto/escape.c src/proto/test_frame.c
	$(CC) $(CFLAGS) src/proto/frame.c src/proto/codec.c src/proto/crc16.c src/proto/escape.c src/proto/test_frame.c -o $@

.PHONY: all clean test
test: all
	FAIL=0; \
	./bin/test_crc16 || FAIL=1; \
	./bin/test_decode || FAIL=1; \
	./bin/test_encode || FAIL=1; \
	./bin/test_escape || FAIL=1; \
	./bin/test_frame || FAIL=1; \
	exit $$FAIL

clean:
	rm -f bin/*


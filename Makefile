CC = gcc
CFLAGS = -Wall -Wextra -Wshadow -std=c11 -Isrc/proto -Isrc/pool

all: bin/test_crc16 bin/test_decode bin/test_encode bin/test_escape bin/test_frame bin/gateway bin/test_pool

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

POOL = src/pool/task_queue.c src/pool/pool.c

# net：IO 胶水层，只进 all，不进 test（真实 socket 由 verify/ 下的验收脚本覆盖）
bin/test_pool: src/pool/test_pool.c $(POOL)
	$(CC) $(CFLAGS) -pthread src/pool/test_pool.c $(POOL) -o $@

bin/gateway: src/net/epoll_server.c $(POOL) src/proto/frame.c src/proto/codec.c src/proto/crc16.c src/proto/escape.c
	$(CC) $(CFLAGS) -pthread src/net/epoll_server.c $(POOL) src/proto/frame.c src/proto/codec.c src/proto/crc16.c src/proto/escape.c -o $@

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


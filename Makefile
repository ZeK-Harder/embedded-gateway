CC = gcc
CFLAGS = -Wall -Wextra -Wshadow -std=c11 -g -Isrc/proto -Isrc/pool -Isrc/store -Isrc/crash
# -g：带调试信息，addr2line 才能把崩溃日志里的偏移还原成"文件:行号"（发布版可以 strip 掉）

all: bin bin/test_crc16 bin/test_decode bin/test_encode bin/test_escape bin/test_frame bin/gateway bin/test_pool bin/store_probe bin/crash_probe bin/loadgen

# bin/ 是被 .gitignore 忽略的：新 clone 下来没有这个目录，
# 不加这一条，gcc -o bin/xxx 会直接报 "No such file or directory"
# 注意这条规则必须写在 all: 之后 —— make 的默认目标是文件里第一个目标
bin:
	mkdir -p bin

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

STORE = src/store/store.c

# store：SQLite 胶水层，只进 all，不进 test（真库由 verify/ 下的验收脚本覆盖）
bin/store_probe: src/store/store_probe.c $(STORE)
	$(CC) $(CFLAGS) -pthread src/store/store_probe.c $(STORE) -lsqlite3 -o $@

CRASH = src/crash/crash.c

# crash：handler 只在真崩时才跑，正常路径覆盖不到它 —— 靠 verify/ 下的探针脚本覆盖
# -rdynamic：把全局符号导进动态符号表，backtrace_symbols_fd 才认得出函数名（学生选 A，2026-10-29）
bin/crash_probe: src/crash/crash_probe.c $(CRASH)
	$(CC) $(CFLAGS) -rdynamic src/crash/crash_probe.c $(CRASH) -o $@

bin/gateway: src/net/epoll_server.c $(POOL) src/proto/frame.c src/proto/codec.c src/proto/crc16.c src/proto/escape.c $(STORE) $(CRASH)
	$(CC) $(CFLAGS) -pthread -rdynamic src/net/epoll_server.c $(POOL) src/proto/frame.c src/proto/codec.c src/proto/crc16.c src/proto/escape.c $(STORE) $(CRASH) -lsqlite3 -o $@

LOADGEN = tools/loadgen/loadgen.c
# loadgen：压测工具（不进 test —— 它要连一个真在跑的网关）
bin/loadgen: $(LOADGEN) src/proto/frame.c src/proto/codec.c src/proto/crc16.c src/proto/escape.c
	$(CC) $(CFLAGS) -pthread $(LOADGEN) src/proto/frame.c src/proto/codec.c src/proto/crc16.c src/proto/escape.c -o $@

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

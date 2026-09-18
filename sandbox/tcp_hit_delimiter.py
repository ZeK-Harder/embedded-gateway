#!/usr/bin/env python3
# 发送方只有 1 条消息，但它的内容里包含一个 \n（0x0A）

import socket
import threading
import time

ADDR = ("127.0.0.1", 9102)
PAYLOAD = b"AB\nCD"        # 这是 1 条消息的内容，中间那个 \n 是数据的一部分


def server():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as srv:
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(ADDR)
        srv.listen(1)
        conn, _ = srv.accept()
        with conn:
            data = conn.recv(1024)
            print(f"[server] 收到的原始字节: {data!r}")
            msgs = data.split(b"\n")
            print(f"[server] 切开的段: {msgs}")
            print(f"[server] 于是我处理了 {len(msgs) - 1} 条消息（忽略末尾空段）")


def client():
    time.sleep(0.3)
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect(ADDR)
        s.send(PAYLOAD + b"\n")          # 发 1 条消息：内容 + 一个真正的分隔符
        print(f"[client] 我发了 1 条消息，内容是: {PAYLOAD!r}")
        time.sleep(0.5)


threading.Thread(target=server, daemon=True).start()
client()

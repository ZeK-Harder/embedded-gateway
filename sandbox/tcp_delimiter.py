#!/usr/bin/env python3
# 每条消息后面加一个 \n，看接收方能不能还原出原本的 3 条消息
import socket
import threading
import time
ADDR = ("127.0.0.1", 9101)
def server():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as srv:
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(ADDR)
        srv.listen(1)
        conn, _ = srv.accept()
        with conn:
            data = conn.recv(1024)
            print(f"[server] 收到的原始字节: {data!r}")
            msgs = data.split(b"\n")          # 用 \n 切开
            print(f"[server] 切开后: {msgs}")
            print(f"[server] 得到 {len(msgs)} 段")
def client():
    time.sleep(0.3)
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect(ADDR)
        for msg in (b"AAAA\n", b"BBBB\n", b"CCCC\n"):
            s.send(msg)
            print(f"[client] send: {msg!r}")
        time.sleep(0.5)
threading.Thread(target=server, daemon=True).start()
client()

#!/usr/bin/env python3
# 这不是项目代码，只是用来亲眼看到现象的工具。
# 问题：客户端分 3 次 send，服务端一次 recv 能拿到什么？
import socket
import threading
import time
ADDR = ("127.0.0.1", 9100)
def server():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as srv:
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(ADDR)
        srv.listen(1)
        conn, _ = srv.accept()
        with conn:
            data = conn.recv(1024)          # 只收这一次，最多收 1024 字节
            print(f"[server] 唯一一次 recv 收到 {len(data)} 字节: {data!r}")
def client():
    time.sleep(0.3)                         # 等 server 起来
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect(ADDR)
        for msg in (b"AAAA", b"BBBB", b"CCCC"):
            s.send(msg)                     # 分三次发
            print(f"[client] send: {msg!r}")
        time.sleep(0.5)
threading.Thread(target=server, daemon=True).start()
client()

#!/usr/bin/env python3
# 转义：数据里出现"特殊字节"时，改用两个字节表示它
# 这一步只演示机制，还没到定协议帧格式
DELIM = 0x7E   # 打算用作帧边界的标记字节
ESC   = 0x7D   # 转义引导字节（"接下来这个字节是改写过的"）
def escape(data: bytes) -> bytes:
    out = bytearray()
    for b in data:
        if b == DELIM:
            out += bytes([ESC, DELIM ^ 0x20])   # 0x7E -> 0x7D 0x5E
        elif b == ESC:
            out += bytes([ESC, ESC ^ 0x20])     # 0x7D -> 0x7D 0x5D
        else:
            out.append(b)                       # 其他字节原样
    return bytes(out)
def unescape(data: bytes) -> bytes:
    out = bytearray()
    i = 0
    while i < len(data):
        if data[i] == ESC:                      # 看到 0x7D，就吃掉两个字节
            out.append(data[i + 1] ^ 0x20)
            i += 2
        else:
            out.append(data[i])
            i += 1
    return bytes(out)
payload = bytes([0x11, 0x7E, 0x22, 0x7D, 0x33])   # 故意混进 0x7E 和 0x7D
print("原始数据     :", payload.hex(" "))
wire = escape(payload)
print("转义后       :", wire.hex(" "))
print("线上还有0x7e?:", DELIM in wire)             # 关键证据
back = unescape(wire)
print("还原后       :", back.hex(" "))
print("还原正确     :", back == payload)

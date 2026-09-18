#!/usr/bin/env python3
# 如果数据里的 0x7D 没有转义，反转义会把它后面的字节一起吃掉

ESC = 0x7D
ORIG = bytes([0x11, 0x7D, 0x41, 0x22])   # 0x7D 是数据本身，发送方忘了转义


def unescape(data):
    out = bytearray()
    i = 0
    while i < len(data):
        if data[i] == ESC:
            out.append(data[i + 1] ^ 0x20)   # 吃掉两个字节
            i += 2
        else:
            out.append(data[i])
            i += 1
    return bytes(out)


print("发送方原始数据:", ORIG.hex(" "))
print("线上实际字节  :", ORIG.hex(" "))          # 因为没转义，线上和原始一样
back = unescape(ORIG)
print("接收方还原    :", back.hex(" "))
print("还原正确?     :", back == ORIG)
print("字节数变化    :", len(ORIG), "->", len(back))

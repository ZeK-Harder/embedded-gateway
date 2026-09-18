#!/usr/bin/env python3
# CRC：给一串字节算一个短指纹，用来发现"内容被改动了"

def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF                      # 初值
    for b in data:
        crc ^= b                      # 把这一字节并进低 8 位
        for _ in range(8):            # 逐位处理
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc                        # 2 字节结果


print("标准向量 123456789 ->", hex(crc16_modbus(b"123456789")))   # 应为 0x4b37
print("空数据             ->", hex(crc16_modbus(b"")))             # 应为 0xffff

frame = bytes([0x11, 0x22, 0x33, 0x44])
a = crc16_modbus(frame)
print("原始数据    :", frame.hex(" "), "-> CRC", hex(a))

broken = bytes([0x11, 0x22, 0x32, 0x44])       # 只把 0x33 改成 0x32，差 1 个 bit
b = crc16_modbus(broken)
print("改 1 bit 后 :", broken.hex(" "), "-> CRC", hex(b))
print("发现了吗?   :", a != b)

extra = frame + b"\x00"                        # 末尾多出 1 个字节
c = crc16_modbus(extra)
print("末尾多 1 字节:", extra.hex(" "), "-> CRC", hex(c))
print("发现了吗?   :", a != c)

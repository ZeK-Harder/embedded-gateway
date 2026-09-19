#ifndef PROTO_H          /* 头文件保护：防止同一个头文件被重复展开，暂时照抄即可 */
#define PROTO_H

#include <stddef.h>      /* size_t */
#include <stdint.h>      /* uint8_t */

/* ===== 协议常量：全部来自 docs/protocol.md，改这里必须同步改文档 ===== */
#define PROTO_FLAG      0x7E   /* 帧头 / 帧尾 */
#define PROTO_ESC       0x7D   /* 转义引导字节 */
#define PROTO_ESC_XOR   0x20   /* 转义变换：原字节 ^ 0x20 */

#define PROTO_LEN_FIXED   6    /* LEN = 6 + payload_len */
#define PROTO_PAYLOAD_MAX 249
#define PROTO_LEN_MAX     255

#define PROTO_FRAME_LOGIC_MAX 256   /* 逻辑帧上限：1（LEN 自身）+ 255 */
#define PROTO_FRAME_WIRE_MAX  514   /* 线上帧上限：256*2 + 2 */

/* 转义：逻辑字节 -> 线上字节
 * 返回实际写入 out 的字节数；out_cap 不足以容纳最坏情况（2*in_len）时返回 0
 */
size_t proto_escape(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap);

/* 反转义：线上字节 -> 逻辑字节
 * 返回实际写入 out 的字节数；输入非法（末尾孤立的 0x7D）或容量不足时返回 0
 */
size_t proto_unescape(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap);

#endif /* PROTO_H */
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

/* 编码：把各字段组装成一整帧（含转义与帧头帧尾）
 *   dev_id / func / seq / payload 为输入，in_len 为载荷长度
 *   out_buf / out_cap 为输出缓冲区及其容量
 *   返回：实际写入 out_buf 的字节数（线上长度）；参数非法或容量不足时返回 0
 *
 * 容量要求：out_cap 至少 2 * (1 + PROTO_LEN_FIXED + payload_len) + 2
 */
size_t proto_encode(uint8_t dev_id, uint8_t func, uint16_t seq,const uint8_t *payload, size_t payload_len,uint8_t *out_buf, size_t out_cap);

/* 解码：把一整帧（含帧头帧尾的线上字节）还原为字段
 *   frame / frame_len：一整帧的线上字节及其长度
 *   dev_id_out / func_out / seq_out：出参，传 NULL 表示不关心该字段
 *   payload_out / payload_cap：载荷输出缓冲区及其容量（必须非 NULL）
 *   payload_len_out：输出实际载荷长度（必须非 NULL）
 *   返回：0 成功；-1 失败（参数非法 / 帧界不对 / 转义非法 / LEN 不一致 / CRC 不符 / 容量不足）
 */
int proto_decode(const uint8_t *frame, size_t frame_len,uint8_t *dev_id_out, uint8_t *func_out, uint16_t *seq_out,uint8_t *payload_out, size_t payload_cap,size_t *payload_len_out);

/* ===== 流式切包状态机 ===== */
/* 解决的问题：TCP 是字节流，一次 recv 可能收到半帧、也可能收到几帧。
 * 本模块按字节喂入，攒够一帧就交出一整帧（含帧头帧尾的线上字节）。
 * 因为状态保存在结构体里，所以与"数据怎么分块送达"无关。 */

typedef enum {
    FRAME_STATE_IDLE = 0,   /* 等待帧头 0x7E */
    FRAME_STATE_COLLECT     /* 帧进行中，收集直到下一个 0x7E */
} frame_state_t;

typedef struct {
    uint8_t       buf[PROTO_FRAME_WIRE_MAX];  /* 含帧头；帧尾到达后也算在内 */
    size_t        len;                        /* 当前已累积字节数 */
    frame_state_t state;
    size_t        stat_frames;                /* 累计成功切出的帧数 */
    size_t        stat_discarded;             /* 累计因超长而丢弃的次数 */
} frame_parser_t;

/* 初始化：清零全部字段。不调用它直接用是未定义行为。 */
void frame_parser_init(frame_parser_t *p);

/* 喂入一个字节。
 *   返回  1：切出一整帧，写入 out_frame，长度写入 *out_len
 *   返回  0：还不够，继续喂
 *   返回 -1：缓冲区超限，本帧已丢弃并重新同步
 *   返回 -2：out_cap 不足（调用方应至少给 PROTO_FRAME_WIRE_MAX）
 */
int frame_parser_feed(frame_parser_t *p, uint8_t byte,uint8_t *out_frame, size_t out_cap, size_t *out_len);

#endif /* PROTO_H */
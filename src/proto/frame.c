#include "proto.h"
#include <string.h>

void frame_parser_init(frame_parser_t *p)
{
    /* memset 一次清干净，比逐字段清零更不容易漏
     * 提醒：你阶段〇在 ringbuf_init 上踩过"赋过值 ≠ 空态"，所以这里要全清
     */
    memset(p, 0, sizeof(frame_parser_t));
    p->state = FRAME_STATE_IDLE;
}


int frame_parser_feed(frame_parser_t *p, uint8_t byte,uint8_t *out_frame, size_t out_cap, size_t *out_len)
{
    /* 第 0 步：防御性检查
     *   p / out_frame / out_len 为 NULL → return -1
     *   out_cap < PROTO_FRAME_WIRE_MAX → return -2
     */
    if(p == NULL || out_frame == NULL || out_len == NULL){
        return -1;
    }
    if(out_cap <  PROTO_FRAME_WIRE_MAX){
        return -2;
    }

    /* 第 1 步：分状态处理。用 switch (p->state) 会比 if/else 更清楚*/
    /* --- 情况 A：FRAME_STATE_IDLE ---
     *   字节不是 0x7E → 没意义，直接 return 0（什么也不做，仍停在 IDLE）
     *   字节是 0x7E   → p->buf[0] = byte;  p->len = 1;
     *                   p->state = FRAME_STATE_COLLECT;
     *                   return 0*/
    /* --- 情况 B：FRAME_STATE_COLLECT ---
     * B1. 先判溢出：如果 p->len >= PROTO_FRAME_WIRE_MAX（缓冲已满还等不到帧尾）
     *       → p->stat_discarded++; p->len = 0; p->state = FRAME_STATE_IDLE;
     *         return -1
     *     位置很重要：必须在写入之前判，否则会越界写
     * B2. 把当前字节收进缓冲：p->buf[p->len] = byte; p->len++;
     * B3. 判断这个字节是不是帧尾（byte == PROTO_FLAG）
     *     ── 不是帧尾 → return 0，继续等
     *     ── 是帧尾：
     *        如果 p->len == 2（说明 buf 里是 "7E 7E"，空帧）
     *            → 不交出；把 buf 收缩成只剩这一个 0x7E 当下一帧的帧头：
     *              p->buf[0] = PROTO_FLAG; p->len = 1;  state 保持 COLLECT
     *              return 0
     *        否则（正常帧）
     *            → 拷贝出去：memcpy(out_frame, p->buf, p->len); *out_len = p->len;
     *              p->stat_frames++;
     *              p->len = 0; p->state = FRAME_STATE_IDLE;
     *              return 1
     */
    switch(p->state){           
        case FRAME_STATE_IDLE:  //返回  1：切出一整帧，写入 out_frame，长度写入 *out_len
        if(byte != 0x7E){       //返回  0：还不够，继续喂
            return 0;           //返回 -1：缓冲区超限，本帧已丢弃并重新同步
        }                       //返回 -2：out_cap 不足（调用方应至少给 PROTO_FRAME_WIRE_MAX）
        else{
            p->buf[0] = byte;
            p->len = 1;
            p->state = FRAME_STATE_COLLECT;
            return 0;
        }
        case FRAME_STATE_COLLECT:
        if(p->len >= PROTO_FRAME_WIRE_MAX){
            p->stat_discarded++;
            p->len = 0;
            p->state = FRAME_STATE_IDLE;
            return -1;
        }
        p->buf[p->len] = byte;
        p->len++;
        if(byte == PROTO_FLAG){
            if(p->len == 2 /*空帧*/){
                p->buf[0] = PROTO_FLAG;
                p->len = 1;
                return 0;
            }
            else{
                memcpy(out_frame, p->buf, p->len);
                *out_len = p->len;
                p->stat_frames++;
                p->len = 0;
                p->state = FRAME_STATE_IDLE;
                return 1;
            }
        }
        else{
            return 0;
        }
    }
    /* 第 2 步：函数末尾兜底 return 0（让编译器满意 -Wreturn-type）*/
    return 0;
}
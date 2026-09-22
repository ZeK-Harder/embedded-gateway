#include "proto.h"
#include "crc16.h"
#include <string.h>

size_t proto_encode(uint8_t dev_id, uint8_t func, uint16_t seq,const uint8_t *payload, size_t payload_len,uint8_t *out_buf, size_t out_cap)
{
    /* 第 0 步：防御性检查
     *   - out_buf 为 NULL
     *   - payload 为 NULL 且 payload_len > 0   ← 注意这里和 CRC 那题同一个道理
     *   - payload_len > PROTO_PAYLOAD_MAX（249）
     *   - 容量不够：out_cap < 2 * (PROTO_LEN_FIXED + payload_len) + 2
     *   任一不满足 → return 0
     * TODO:
     */
    if(out_buf == NULL || (payload == NULL && payload_len > 0) || payload_len > PROTO_PAYLOAD_MAX || out_cap < 2 * (1 + PROTO_LEN_FIXED + payload_len) + 2){
        return 0;
    }

    /* 第 1 步：在栈上准备一块临时缓冲区 logic[]，用来装"逻辑帧"（转义前的）
     *   大小：PROTO_FRAME_LOGIC_MAX（256）就够
     *   写入下标 w = 0
     *   注意：这里不写帧头，从 LEN 字段开始写
     * TODO:
     */
    uint8_t logic[PROTO_FRAME_LOGIC_MAX];
    size_t w = 0; 
     
    /* 第 2 步：写 LEN 字段
     *   len = PROTO_LEN_FIXED + payload_len
     *   logic[w] = (uint8_t)len;  w++;
     *   （LEN 是 1 字节，不需要拆高地位）
     * TODO:
     */
    size_t len = PROTO_LEN_FIXED + payload_len;
    logic[w] = (uint8_t)len;    w++;
    
    /* 第 3 步：写 DEV_ID 和 FUNC（各 1 字节）
     * TODO:
     */
    logic[w] = dev_id;  w++;
    logic[w] = func;    w++;
    
    /* 第 4 步：写 SEQ（2 字节，大端序：先高字节后低字节）
     *   logic[w] = (uint8_t)(seq >> 8);  w++;
     *   logic[w] = (uint8_t)(seq & 0xFF); w++;
     *   想想为什么 (uint8_t) 这个强制转换不能省
     * TODO:
     */
    logic[w] = (uint8_t)(seq >> 8);     w++;
    logic[w] = (uint8_t)(seq & 0xFF);   w++;

    /* 第 5 步：拷贝 PAYLOAD（可能 0 字节，用循环或 memcpy 都可以）
     * TODO:
     */
    for(size_t i = 0;i < payload_len;i++){
        logic[w] = payload[i];
        w++;
    }

    /* 第 6 步：算 CRC16
     *   crc = crc16_modbus(logic, w);   ← 覆盖范围就是 w 个逻辑字节
     *   然后把 crc 按大端序追加到 logic[w] / logic[w+1]，w += 2
     * TODO:
     */
    uint16_t crc = crc16_modbus(logic, w);
    logic[w++] = (uint8_t)(crc >> 8);      /* 高字节在前 */
    logic[w++] = (uint8_t)(crc & 0xFF);    /* 低字节在后 */


    /* 第 7 步：转义 + 加帧头帧尾，直接写进调用方的 out_buf
     *   out_buf[0] = PROTO_FLAG;
     *   n = proto_escape(logic, w, out_buf + 1, out_cap - 1);
     *   n == 0 说明出了意外（容量已在上一步查过），return 0
     *   out_buf[1 + n] = PROTO_FLAG;
     *   返回 1 + n + 1
     * TODO:
     */
    out_buf[0] = PROTO_FLAG;
    size_t n = proto_escape(logic, w, out_buf + 1, out_cap - 1);
    if(n == 0){
        return 0;
    }
    out_buf[1 + n] = PROTO_FLAG;
    return 1 + n + 1;
}

int proto_decode(const uint8_t *frame, size_t frame_len,uint8_t *dev_id_out, uint8_t *func_out, uint16_t *seq_out,uint8_t *payload_out, size_t payload_cap,size_t *payload_len_out)
{
    /* 第 0 步：防御性检查 
     * - frame 为 NULL
     * - payload_out 为 NULL、payload_len_out 为 NULL
     * - frame_len < 9（最短线上帧：7e + 6 个逻辑字节 + 1 个帧尾... 数一下：LEN 1 + DEV 1 + FUNC 1 + SEQ 2 + CRC 2 = 7，加两个 7e = 9）
     * - frame_len > PROTO_FRAME_WIRE_MAX（514）
     *   任一不满足 → return -1
     */
    if (frame == NULL || payload_out == NULL || payload_len_out == NULL) {
        return -1;
    }
    if (frame_len < 9 || frame_len > PROTO_FRAME_WIRE_MAX) {
        return -1;
    }

    /* 第 1 步：帧头帧尾
     * frame[0] 和 frame[frame_len - 1] 都必须等于 PROTO_FLAG
     */
    if (frame[0] != PROTO_FLAG || frame[frame_len - 1] != PROTO_FLAG) {
        return -1;
    }

    /* 第 2 步：反转义
     * 中间部分从 frame + 1 开始，长度 frame_len - 2
     * 反转义到栈上的 uint8_t logic[PROTO_FRAME_LOGIC_MAX]
     * 返回 0（转义非法或容量不足）→ return -1
     * 记下 logic_len
     */
    uint8_t logic[PROTO_FRAME_LOGIC_MAX];
    size_t logic_len = proto_unescape(frame + 1, frame_len - 2, logic, sizeof logic);
    if (logic_len == 0) {
        return -1;
    }

    /* 第 3 步：最小长度与 LEN 自洽检查（放在 CRC 之前，理由见下）
     *   - logic_len < 7 → return -1（连字段都装不下）
     *   - LEN 一致性：logic[0] 必须等于 logic_len - 1
     *     想清楚为什么是 -1：LEN 数的是"它自己之外"的字节
     *   - 顺带检查逻辑帧不超过 256（logic_len <= PROTO_FRAME_LOGIC_MAX）
     */
    if (logic_len < 7 || logic_len > PROTO_FRAME_LOGIC_MAX) {
        return -1;
    }
    if ((size_t)logic[0] != logic_len - 1) {
        return -1;
    }

    /* 第 4 步：CRC 校验
     *   接收方算出来的：crc16_modbus(logic, logic_len - 2)   ← 最后 2 字节是 CRC 本身，不参与计算
     *   帧里带的：  (uint16_t)(logic[logic_len - 2] << 8) | logic[logic_len - 1]
     *   两者不等 → return -1
     */
    uint16_t crc_calc = crc16_modbus(logic, logic_len - 2);
    uint16_t crc_recv = (uint16_t)((uint16_t)logic[logic_len - 2] << 8) | (uint16_t)logic[logic_len - 1];
    if (crc_calc != crc_recv) {
        return -1;
    }

    /* 第 5 步：算载荷长度并拷贝
     *   payload_len = logic_len - 1 - PROTO_LEN_FIXED
     *   payload_len > payload_cap → return -1
     *   载荷字节从 logic[5] 开始（LEN 1 + DEV_ID 1 + FUNC 1 + SEQ 2 = 5 字节），拷 payload_len 个
     *   *payload_len_out = payload_len
     */
    size_t payload_len = logic_len - 1 - PROTO_LEN_FIXED;   /* = logic_len - 7 */
    if (payload_len > payload_cap) {
        return -1;
    }
    memcpy(payload_out, logic + 5, payload_len);
    *payload_len_out = payload_len;

    /* 第 6 步：拆字段给出参（每个出参都要先判 NULL）
     *   dev_id: logic[1]
     *   func  : logic[2]
     *   seq   : (uint16_t)(logic[3] << 8) | logic[4]
     */
    if (dev_id_out != NULL) {
        *dev_id_out = logic[1];
    }
    if (func_out != NULL) {
        *func_out = logic[2];
    }
    if (seq_out != NULL) {
        *seq_out = (uint16_t)((uint16_t)logic[3] << 8) | (uint16_t)logic[4];
    }

    /* 第 7 步：return 0*/
    return 0;
}
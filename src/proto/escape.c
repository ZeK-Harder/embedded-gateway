#include "proto.h"

size_t proto_escape(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap)
{
    /* 第 0 步：防御性检查（对应你阶段〇的"分配→检查→使用"三拍子）
     *   如果 in 或 out 是 NULL，或者 out_cap 连最坏情况（2 * in_len）都装不下，
     *   直接 return 0。先检查完再干活，中途就不用反复判断容量了。
     * TODO:
     */
    if(in == NULL || out == NULL || out_cap < 2 * in_len){
        return 0;
    }
    /* 第 1 步：准备一个"当前写到哪了"的下标 w，从 0 开始
     * TODO:
     */
    size_t w = 0;
    /* 第 2 步：用 for 循环逐字节扫描 in，i 从 0 到 in_len - 1
     * TODO:
     */
    /* 第 3 步：循环里判断当前字节 b = in[i]
     *   b 是 0x7E 或 0x7D  →  往 out 写两个字节：先写 PROTO_ESC，再写 (b ^ PROTO_ESC_XOR)
     *   其它字节           →  往 out 写一个字节 b
     *   每写一个字节，w 都要往后推进一位（写两个就推进两次）
     * TODO:
     */
    for(size_t i = 0 ; i < in_len ; i++){
        uint8_t b = in[i];
        if(b == 0x7E || b == 0x7D){
            out[w] = PROTO_ESC;
            out[w+1] = (b ^ PROTO_ESC_XOR);
            w += 2;
        }
        else{
            out[w] = b;
            w++;
        }    
    }

    /* 第 4 步：返回 w —— 真实写入的字节数
     *   不要返回 out_cap，也不要直接返回 in_len
     * TODO:
     */
     return w;
}

size_t proto_unescape(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap)
{
    /* 第 0 步：防御性检查
     *   NULL 检查；out_cap 只要 >= in_len 就一定够（反转义只会变短或不变）
     * TODO:
     */
    if(in == NULL || out == NULL || out_cap < in_len){
        return 0;
    }
    /* 第 1 步：两个下标：i 读 in，w 写 out，都从 0 开始
     * TODO:
     */
    size_t i = 0;
    size_t w = 0;
    /* 第 2 步：while (i < in_len) 逐字节处理，分两种情况：
     *   情况 A：in[i] == PROTO_ESC
     *       先检查 i + 1 是否已经越界 —— 越界说明输入被截断（末尾孤立的 0x7D），直接 return 0
     *       否则 out[w] = in[i + 1] ^ PROTO_ESC_XOR，i 前进 2，w 前进 1
     *   情况 B：其它字节
     *       out[w] = in[i]，i 前进 1，w 前进 1
     * TODO:
     */
    while (i < in_len){
        if(in[i] == PROTO_ESC){
            if(i+1 >= in_len){
                return 0;
            }
            else{
                out[w] = in[i + 1] ^ PROTO_ESC_XOR;
                i += 2;
                w++;
            }
        }
        else{
            out[w] = in[i];
            i++;
            w++;
        }
    }
    /* 第 3 步：return w
     * TODO:
     */
     return w;
}
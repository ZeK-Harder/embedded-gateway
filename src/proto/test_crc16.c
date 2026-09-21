#include <stdio.h>
#include "crc16.h"

int main(void)
{
    /* 用例 1：标准校验向量，应得 0x4B37 —— 与 Python 版、阶段〇自测结果一致 */
    const uint8_t vector[] = "123456789";
    printf("\"123456789\" -> 0x%04X（预期 0x4B37）\n", crc16_modbus(vector, 9));

    /* 用例 2：空数据返回初值 */
    printf("空数据      -> 0x%04X（预期 0xFFFF）\n", crc16_modbus(NULL, 0));

    /* 用例 3：改动 1 个 bit 必须导致校验值变化（检出能力） */
    uint8_t frame[] = {0x11, 0x22, 0x33, 0x44};
    uint16_t before = crc16_modbus(frame, sizeof frame);
    frame[2] = 0x32;                                  /* 0x33 -> 0x32，差 1 bit */
    uint16_t after = crc16_modbus(frame, sizeof frame);
    printf("改 1 bit: 0x%04X -> 0x%04X -> %s\n",
           before, after, (before != after) ? "检出" : "未检出");

    return 0;
}
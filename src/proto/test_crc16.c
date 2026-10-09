#include <stdio.h>
#include "crc16.h"

static int test_vector(void)
{
    const uint8_t vector[] = "123456789";
    uint16_t got = crc16_modbus(vector, 9);
    int pass = (got == 0x4B37);
    printf("\"123456789\" -> 0x%04X（预期 0x4B37）  %s\n", got, pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

static int test_empty(void)
{
    uint16_t got = crc16_modbus(NULL, 0);
    int pass = (got == 0xFFFF);
    printf("空数据      -> 0x%04X（预期 0xFFFF）  %s\n", got, pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

static int test_bitflip(void)
{
    uint8_t frame[] = {0x11, 0x22, 0x33, 0x44};
    uint16_t before = crc16_modbus(frame, sizeof frame);
    frame[2] = 0x32;
    uint16_t after = crc16_modbus(frame, sizeof frame);
    int pass = (before != after);
    printf("改 1 bit: 0x%04X -> 0x%04X -> %s\n", before, after, pass ? "检出 PASS" : "未检出 FAIL");
    return pass ? 0 : 1;
}

int main(void)
{
    int failed = 0;
    failed += test_vector();
    failed += test_empty();
    failed += test_bitflip();
    printf("\n共 %d 条用例失败\n", failed);
    return failed;
}

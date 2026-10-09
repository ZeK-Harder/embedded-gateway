#include <stdio.h>
#include <string.h>
#include "proto.h"

static int test_normal(void)
{
    uint8_t frame[600];
    const uint8_t payload[] = {0x01, 0x02, 0x03};
    size_t n = proto_encode(0x11, 0x01, 0x0001, payload, sizeof payload, frame, sizeof frame);
    printf("用例1 普通帧: 返回 %zu（预期 12）\n", n);
    int pass = (n == 12);

    uint8_t expect[12] = {
        0x7E, 0x09, 0x11, 0x01, 0x00, 0x01,
        0x01, 0x02, 0x03,
        0x00, 0x00,
        0x7E
    };
    if (n >= 12) {
        if (frame[0] != expect[0] || frame[1] != expect[1]
            || frame[2] != expect[2] || frame[3] != expect[3]
            || frame[4] != expect[4] || frame[5] != expect[5]
            || frame[6] != expect[6] || frame[7] != expect[7]
            || frame[8] != expect[8] || frame[11] != expect[11]) {
            pass = 0;
        }
    } else {
        pass = 0;
    }

    uint8_t dev, func, back[300];
    uint16_t seq;
    size_t plen;
    int rc = proto_decode(frame, n, &dev, &func, &seq, back, sizeof back, &plen);
    if (rc != 0 || dev != 0x11 || func != 0x01 || seq != 0x0001
        || plen != sizeof payload || memcmp(back, payload, plen) != 0) {
        pass = 0;
    }
    printf("    硬字节校验 + 回环校验  %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}


static int test_escape(void)
{
    uint8_t frame[600];
    const uint8_t payload[] = {0x7E, 0x7D, 0xFF};
    size_t n = proto_encode(0x22, 0x01, 0x0102, payload, sizeof payload, frame, sizeof frame);
    printf("用例2 含转义: 返回 %zu（预期 14）\n", n);
    int pass = (n == 14);
    printf("    %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

static int test_empty(void)
{
    uint8_t frame[600];
    size_t n = proto_encode(0x33, 0x01, 0x0003, NULL, 0, frame, sizeof frame);
    printf("用例3 空载荷: 返回 %zu（预期 9）\n", n);
    int pass = (n == 9);
    printf("    %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

static int test_payload_overflow(void)
{
    uint8_t frame[600];
    uint8_t big[300];
    memset(big, 0xAA, sizeof big);
    size_t n = proto_encode(0x11, 0x01, 0x0001, big, sizeof big, frame, sizeof frame);
    printf("用例4 超上限: 返回 %zu（预期 0）\n", n);
    int pass = (n == 0);
    printf("    %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

static int test_buffer_too_small(void)
{
    uint8_t tiny[5];
    const uint8_t payload[] = {0x01, 0x02, 0x03};
    size_t n = proto_encode(0x11, 0x01, 0x0001, payload, sizeof payload, tiny, sizeof tiny);
    printf("用例5 容量不足: 返回 %zu（预期 0）\n", n);
    int pass = (n == 0);
    printf("    %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

int main(void)
{
    int failed = 0;
    failed += test_normal();
    failed += test_escape();
    failed += test_empty();
    failed += test_payload_overflow();
    failed += test_buffer_too_small();
    printf("\n共 %d 条用例失败\n", failed);
    return failed;
}

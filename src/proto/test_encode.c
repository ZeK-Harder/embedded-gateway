#include <stdio.h>
#include <string.h>
#include "proto.h"

static void dump(const char *tag, const uint8_t *buf, size_t len)
{
    printf("%-12s len=%2zu :", tag, len);
    for (size_t i = 0; i < len; i++) {
        printf(" %02x", buf[i]);
    }
    printf("\n");
}

int main(void)
{
    uint8_t frame[600];

    /* 用例 1：最普通的帧 */
    const uint8_t payload[] = {0x01, 0x02, 0x03};
    size_t n = proto_encode(0x11, 0x01, 0x0001, payload, sizeof payload,
                            frame, sizeof frame);
    dump("用例1", frame, n);

    /* 用例 2：载荷里含 0x7E 和 0x7D，应看到转义后帧变长 */
    const uint8_t tricky[] = {0x7E, 0x7D, 0xFF};
    size_t n2 = proto_encode(0x22, 0x01, 0x0102, tricky, sizeof tricky,
                             frame, sizeof frame);
    dump("用例2", frame, n2);

    /* 用例 3：空载荷（payload_len = 0，payload 传 NULL） */
    size_t n3 = proto_encode(0x33, 0x01, 0xFFFF, NULL, 0, frame, sizeof frame);
    dump("用例3", frame, n3);

    /* 用例 4：载荷超限（249 是上限，250 应失败返回 0） */
    uint8_t big[250];
    memset(big, 0xAA, sizeof big);
    printf("用例4 载荷250字节 -> 返回 %zu（预期 0）\n",
           proto_encode(0x44, 0x01, 0x0002, big, sizeof big, frame, sizeof frame));

    /* 用例 5：容量不足 */
    printf("用例5 容量不足 -> 返回 %zu（预期 0）\n",
           proto_encode(0x55, 0x01, 0x0003, payload, sizeof payload, frame, 5));

    return 0;
}
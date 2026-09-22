#include <stdio.h>
#include <string.h>
#include "proto.h"

static void try_decode(const char *tag, const uint8_t *frame, size_t len, int expect_ok)
{
    uint8_t dev = 0, func = 0, payload[300];
    uint16_t seq = 0;
    size_t plen = 0;

    int rc = proto_decode(frame, len, &dev, &func, &seq, payload, sizeof payload, &plen);
    printf("%-18s rc=%2d (预期 %2d)", tag, rc, expect_ok ? 0 : -1);
    if (rc == 0) {
        printf("  dev=%02x func=%02x seq=%04x plen=%zu payload=", dev, func, seq, plen);
        for (size_t i = 0; i < plen; i++) {
            printf("%02x", payload[i]);
        }
    }
    printf("\n");
}

int main(void)
{
    uint8_t frame[600];
    size_t n;

    const uint8_t p1[] = {0x01, 0x02, 0x03};
    const uint8_t p2[] = {0x7E, 0x7D, 0xFF};

    /* 用例 1：正常往返，载荷应为 010203 */
    n = proto_encode(0x11, 0x01, 0x0001, p1, sizeof p1, frame, sizeof frame);
    try_decode("用例1 正常帧", frame, n, 1);

    /* 用例 2：载荷含转义字节，还原后应为 7e7dff */
    n = proto_encode(0x22, 0x01, 0x0102, p2, sizeof p2, frame, sizeof frame);
    try_decode("用例2 含转义", frame, n, 1);

    /* 用例 3：帧头被破坏 */
    n = proto_encode(0x11, 0x01, 0x0001, p1, sizeof p1, frame, sizeof frame);
    frame[0] = 0x00;
    try_decode("用例3 帧头错", frame, n, 0);

    /* 用例 4：CRC 高字节被改 1 bit（倒数第 3 字节） */
    n = proto_encode(0x11, 0x01, 0x0001, p1, sizeof p1, frame, sizeof frame);
    frame[n - 3] ^= 0x01;
    try_decode("用例4 CRC错", frame, n, 0);

    /* 用例 5：LEN 被改（frame[1] 是 LEN，09 -> 0a） */
    n = proto_encode(0x11, 0x01, 0x0001, p1, sizeof p1, frame, sizeof frame);
    frame[1] = 0x0A;
    try_decode("用例5 LEN改坏", frame, n, 0);

    /* 用例 6：非法转义 —— 帧内出现孤立的 0x7D（9 字节，能进到反转义环节） */
    uint8_t bad[] = {0x7E, 0x06, 0x11, 0x01, 0x00, 0x01, 0x7D, 0x7E};
    try_decode("用例6 孤立转义", bad, sizeof bad, 0);
    /* 用例 7：长度不足 */
    uint8_t tiny[] = {0x7E, 0x06, 0x7E};
    try_decode("用例7 过短", tiny, sizeof tiny, 0);

    return 0;
}
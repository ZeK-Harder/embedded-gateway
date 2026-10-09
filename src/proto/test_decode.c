#include <stdio.h>
#include <string.h>
#include "proto.h"

static int try_decode(const char *tag, const uint8_t *frame, size_t len, int expect_ok)
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
    int pass = ((rc == 0) == expect_ok);
    printf("  %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

int main(void)
{
    uint8_t frame[600];
    size_t n;
    int failed = 0;
    const uint8_t p1[] = {0x01, 0x02, 0x03};
    const uint8_t p2[] = {0x7E, 0x7D, 0xFF};

    n = proto_encode(0x11, 0x01, 0x0001, p1, sizeof p1, frame, sizeof frame);
    failed += try_decode("用例1 正常帧", frame, n, 1);

    n = proto_encode(0x22, 0x01, 0x0102, p2, sizeof p2, frame, sizeof frame);
    failed += try_decode("用例2 含转义", frame, n, 1);

    n = proto_encode(0x11, 0x01, 0x0001, p1, sizeof p1, frame, sizeof frame);
    frame[0] = 0x00;
    failed += try_decode("用例3 帧头错", frame, n, 0);

    n = proto_encode(0x11, 0x01, 0x0001, p1, sizeof p1, frame, sizeof frame);
    frame[n - 3] ^= 0x01;
    failed += try_decode("用例4 CRC错", frame, n, 0);

    n = proto_encode(0x11, 0x01, 0x0001, p1, sizeof p1, frame, sizeof frame);
    frame[1] = 0x0A;
    failed += try_decode("用例5 LEN改坏", frame, n, 0);

    uint8_t bad[] = {0x7E, 0x06, 0x11, 0x01, 0x00, 0x01, 0x7D, 0x7E};
    failed += try_decode("用例6 孤立转义", bad, sizeof bad, 0);

    uint8_t tiny[] = {0x7E, 0x06, 0x7E};
    failed += try_decode("用例7 过短", tiny, sizeof tiny, 0);

    printf("\n共 %d 条用例失败\n", failed);
    return failed;
}

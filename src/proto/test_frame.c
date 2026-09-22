#include <stdio.h>
#include <string.h>
#include "proto.h"

static frame_parser_t P;
static int frames_out;

static void show_frame(const uint8_t *frame, size_t flen)
{
    uint8_t dev = 0, func = 0, payload[300];
    uint16_t seq = 0;
    size_t plen = 0;
    int rc = proto_decode(frame, flen, &dev, &func, &seq, payload, sizeof payload, &plen);
    printf("    第 %d 帧 len=%2zu decode=%s dev=%02x seq=%04x plen=%zu\n",
           ++frames_out, flen, (rc == 0 ? "OK" : "FAIL"), dev, seq, plen);
}

/* 把一段字节逐个喂进去，统计切出的帧数 */
static int feed(const uint8_t *data, size_t len)
{
    uint8_t frame[PROTO_FRAME_WIRE_MAX];
    size_t flen;
    int cnt = 0;
    for (size_t i = 0; i < len; i++) {
        int rc = frame_parser_feed(&P, data[i], frame, sizeof frame, &flen);
        if (rc == 1) { cnt++; show_frame(frame, flen); }
        else if (rc == -1) { printf("    [丢弃] 超长帧，重新同步\n"); }
    }
    return cnt;
}

int main(void)
{
    uint8_t f1[600], f2[600], f3[600], stream[2000];
    const uint8_t p1[] = {0x01, 0x02, 0x03};
    const uint8_t p2[] = {0x7E, 0x7D, 0xFF};
    const uint8_t p3[] = {0xAA};
    size_t n1, n2, n3;

    n1 = proto_encode(0x11, 0x01, 0x0001, p1, sizeof p1, f1, sizeof f1);
    n2 = proto_encode(0x22, 0x01, 0x0102, p2, sizeof p2, f2, sizeof f2);
    n3 = proto_encode(0x33, 0x01, 0x0003, p3, sizeof p3, f3, sizeof f3);
    printf("f1=%zu字节 f2=%zu字节 f3=%zu字节\n\n", n1, n2, n3);

    /* 场景 1：一帧拆成两半送（半包） */
    printf("场景1 半包（分两次喂）\n");
    frame_parser_init(&P); frames_out = 0;
    feed(f1, n1 / 2);
    feed(f1 + n1 / 2, n1 - n1 / 2);
    printf("    → 共切出 %d 帧（预期 1） 残留 len=%zu（预期 0）\n\n", frames_out, P.len);

    /* 场景 2：两帧粘在一起送（粘包） */
    printf("场景2 粘包（两帧连发）\n");
    frame_parser_init(&P); frames_out = 0;
    memcpy(stream, f1, n1); memcpy(stream + n1, f2, n2);
    feed(stream, n1 + n2);
    printf("    → 共切出 %d 帧（预期 2） 残留 len=%zu（预期 0）\n\n", frames_out, P.len);

    /* 场景 3：验收标准里的 2.5 个包 */
    printf("场景3 2.5 个包（f1+f2+半个f3）\n");
    frame_parser_init(&P); frames_out = 0;
    memcpy(stream, f1, n1);
    memcpy(stream + n1, f2, n2);
    memcpy(stream + n1 + n2, f3, n3 / 2);
    feed(stream, n1 + n2 + n3 / 2);
    printf("    → 共切出 %d 帧（预期 2） 残留 len=%zu（预期 %zu）\n\n",
           frames_out, P.len, n3 / 2);

    /* 场景 4：空帧（连续两个 0x7E）不应被当成一帧 */
    printf("场景4 连续两个0x7E（空帧）\n");
    frame_parser_init(&P); frames_out = 0;
    {
        uint8_t empty[] = {0x7E, 0x7E, 0x7E, 0x11, 0x22, 0x7E, 0x7E};
        feed(empty, sizeof empty);
    }
    printf("    → 共切出 %d 帧（预期 0：内容都不足一帧）\n\n", frames_out);

    /* 场景 5：超长无帧尾，应丢弃并重新同步，随后仍能正常切帧 */
    printf("场景5 超长帧后紧跟一个正常帧\n");
    frame_parser_init(&P); frames_out = 0;
    memset(stream, 0xAA, 600);
    memcpy(stream + 600, f1, n1);
    feed(stream, 600 + n1);
    printf("    → 共切出 %d 帧（预期 1） 丢弃计数=%zu（预期 1）\n", frames_out, P.stat_discarded);

    return 0;
}
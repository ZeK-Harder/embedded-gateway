#include <stdio.h>
#include <string.h>
#include "proto.h"

static void dump(const char *tag, const uint8_t *buf, size_t len)
{
    printf("%-10s len=%zu :", tag, len);
    for (size_t i = 0; i < len; i++) {
        printf(" %02x", buf[i]);
    }
    printf("\n");
}

static int test_roundtrip(void)
{
    uint8_t in[] = {0x11, 0x7E, 0x22, 0x7D, 0x33};
    uint8_t out[64], back[64];
    size_t n = proto_escape(in, sizeof in, out, sizeof out);
    dump("原始", in, sizeof in);
    dump("转义后", out, n);
    size_t m = proto_unescape(out, n, back, sizeof back);
    dump("还原后", back, m);
    int pass = (m == sizeof in && memcmp(in, back, m) == 0);
    printf("往返一致: %s\n", pass ? "是 PASS" : "否 FAIL");
    return pass ? 0 : 1;
}

static int test_many_7e(void)
{
    uint8_t many[100];
    uint8_t big[256];
    memset(many, 0x7E, sizeof many);
    size_t got = proto_escape(many, sizeof many, big, sizeof big);
    int pass = (got == 200);
    printf("100 个 0x7E -> 线上 %zu 字节（预期 200）  %s\n", got, pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

static int test_orphan_7d(void)
{
    uint8_t bad[] = {0x11, 0x7D};
    uint8_t back[64];
    size_t got = proto_unescape(bad, sizeof bad, back, sizeof back);
    int pass = (got == 0);
    printf("末尾孤立 0x7D -> 返回 %zu（预期 0）  %s\n", got, pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

int main(void)
{
    int failed = 0;
    failed += test_roundtrip();
    failed += test_many_7e();
    failed += test_orphan_7d();
    printf("\n共 %d 条用例失败\n", failed);
    return failed;
}

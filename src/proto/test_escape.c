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

int main(void)
{
    /* 用例 1：往返一致性（和你 Python 版用的同一组数据） */
    uint8_t in[] = {0x11, 0x7E, 0x22, 0x7D, 0x33};
    uint8_t out[64], back[64];

    size_t n = proto_escape(in, sizeof in, out, sizeof out);
    dump("原始", in, sizeof in);
    dump("转义后", out, n);

    size_t m = proto_unescape(out, n, back, sizeof back);
    dump("还原后", back, m);
    printf("往返一致: %s\n",
           (m == sizeof in && memcmp(in, back, m) == 0) ? "是" : "否");

    /* 用例 2：100 个 0x7E，线上应为 200 字节 */
    uint8_t many[100];
    uint8_t big[256];
    memset(many, 0x7E, sizeof many);
    printf("100 个 0x7E -> 线上 %zu 字节（预期 200）\n",
           proto_escape(many, sizeof many, big, sizeof big));

    /* 用例 3：非法输入 —— 末尾孤立的 0x7D，应返回 0 */
    uint8_t bad[] = {0x11, 0x7D};
    printf("末尾孤立 0x7D -> 返回 %zu（预期 0）\n",
           proto_unescape(bad, sizeof bad, back, sizeof back));

    return 0;
}
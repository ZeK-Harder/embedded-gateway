/* 任务队列探针
 * 用法：见本轮验收命令。它只用 task_queue.h 里声明的 5 个函数。 */
#include <stdio.h>
#include "task_queue.h"

static void show(void *arg)
{
    int *p = arg;
    printf("%d", *p);
}

int main(void)
{
    struct task_queue q;
    struct task t, out;
    int a = 10, b = 20, c = 30, d = 40, e = 50, rc;

    if (taskq_init(&q, 4) != 0) { printf("init FAIL\n"); return 1; }

    t.fn = show;
    t.arg = &a; printf("push a -> %d\n", taskq_push(&q, t));
    t.arg = &b; printf("push b -> %d\n", taskq_push(&q, t));
    t.arg = &c; printf("push c -> %d\n", taskq_push(&q, t));
    t.arg = &d; printf("push d -> %d\n", taskq_push(&q, t));
    t.arg = &e; printf("push e(第5个,应-1) -> %d\n", taskq_push(&q, t));

    rc = taskq_pop(&q, &out);
    printf("pop1 -> %d 值=", rc); if (rc == 0) { out.fn(out.arg); } printf("\n");

    t.arg = &e; printf("push e(回绕后,应0) -> %d\n", taskq_push(&q, t));
    printf("size -> %zu\n", taskq_size(&q));

    while ((rc = taskq_pop(&q, &out)) == 0) { printf("pop -> "); out.fn(out.arg); printf("\n"); }
    printf("pop 空 -> %d\n", rc);

    taskq_free(&q);
    return 0;
}

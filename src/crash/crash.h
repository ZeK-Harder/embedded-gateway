/* src/crash/crash.h —— 崩溃现场记录器的对外合同
 *
 * 只做一件事：进程被 SIGSEGV / SIGABRT / SIGBUS / SIGFPE / SIGILL 打死之前，
 * 往一个**早就打开好的文件**里写一条记录（哪个信号、内核给的 si_code、出错地址、哪个线程）。
 *
 * 为什么值一个独立模块：崩溃那一刻进程已经不可信了 ——
 * 不能 printf（要 malloc、要 stdio 锁）、不能 fopen（可能失败、可能死锁），只能 write()。
 * 所以本模块只许调 POSIX 的 async-signal-safe 函数（清单见 `man 7 signal-safety`）。
 */
#ifndef CRASH_H
#define CRASH_H

/* 装 handler。
 *   path —— 崩溃日志路径；传 NULL 用默认值 /tmp/gateway-crash.log
 * 返回：0 = 装好了；-1 = 打不开日志文件或 sigaction 失败（errno 有意义）。
 *
 * 设计取舍（2026-10-29 学生拍板 A）：**fd 在启动期就预打开并常驻**，
 * 于是路径 / 权限 / 磁盘满这些问题在启动时就暴露，崩溃那一刻只剩一次 write()。
 * 代价：全程多占 1 个 fd —— 所以带 O_CLOEXEC，不许漏给 exec 出来的子进程。
 *
 * 调用方责任：返回 -1 就**当场退出**（崩溃日志开不了 = 这程序出事后没人知道为什么）。
 */
int crash_install(const char *path);

#endif /* CRASH_H */

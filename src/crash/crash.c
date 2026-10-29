/* src/crash/crash.c —— 崩溃现场记录器
 *
 * 一条铁律贯穿全文件：**handler 里只许调 async-signal-safe 函数**。
 * 也就是：只用 write()，不用 printf / fprintf / snprintf / malloc / free / exit / syslog。
 * 本文件里所有 put_xxx 都是在"自己手搓格式化"—— 因为 snprintf 不在安全清单里。
 */
#define _GNU_SOURCE
/* Dl_info / dladdr 在 glibc 里只有 _GNU_SOURCE 才亮出来（它们不在 POSIX 清单里）；
 * 只给 _POSIX_C_SOURCE 的话编译器会报 unknown type name 'Dl_info'（2026-10-29 踩过）。
 * 注意：_GNU_SOURCE 必须在**任何 include 之前**定义。 */
#define _POSIX_C_SOURCE 200809L
/* -std=c11 会让 __STRICT_ANSI__ 生效，于是 unistd.h 里 syscall() 的声明被藏起来 →
 * 隐式声明 = 一条编译警告。补 _DEFAULT_SOURCE 把它请出来（只为 syscall(SYS_gettid)）。 */
#define _DEFAULT_SOURCE

#include <signal.h>        // sigaction / sigemptyset / SA_SIGINFO
#include <fcntl.h>         // open / O_WRONLY / O_CREAT / O_APPEND / O_CLOEXEC
#include <unistd.h>        // write / _exit / getpid
#include <string.h>        // memset
#include <errno.h>         // errno / EINTR
#include <sys/syscall.h>   // SYS_gettid（gettid 不是 POSIX 函数，直接走 syscall）
#include <execinfo.h>      // backtrace / backtrace_symbols_fd
#include <dlfcn.h>         // dladdr：只在启动期取一次装载基址，不在信号上下文里
#include "crash.h"

/* 最多记多少层栈：够看清"谁调的谁"，又不至于把一条记录写成几千行 */
#define CRASH_BT_DEPTH 32

/* 崩溃日志的 fd：crash_install 里打开一次，之后全程只读不改。
 * -1 = 没装（handler 里 write(-1) 会立刻 EBADF 返回，不会崩第二次）。 */
static int g_fd = -1;

/* 本程序被装载到内存的基址。Ubuntu 默认 PIE：运行期地址 = 基址 + 文件内偏移，
 * 而 addr2line 要的是**偏移**。启动期记下来，事后「地址 - 基址」就能还原到源文件行号。
 * （学生 2026-10-29 选 B：保留 PIE/ASLR，用记基址换还原的一步减法） */
static unsigned long g_base = 0;

/* ---------- 手搓输出：全部只依赖 write() ---------- */

/* 把 len 个字节写完。写不完（EINTR）就重来；真失败就放弃 ——
 * handler 里没有第二层失败处理可用（没有日志系统、没有返回值接收者）。 */
static void put_mem(const char *s, size_t len)
{
    while (len > 0) {
        ssize_t n = write(g_fd, s, len);
        if (n < 0) {
            if (errno == EINTR) continue;
            return;
        }
        s += n;
        len -= (size_t)n;
    }
}

static void put_str(const char *s)
{
    size_t len = 0;
    while (s[len] != '\0') len++;   /* 不用 strlen：省得纠结它在不在安全清单里 */
    put_mem(s, len);
}

/* 十六进制，带 0x 前缀、掐掉前导 0（至少留一位）：0 → "0x0"，NULL 指针好认 */
static void put_hex(unsigned long v)
{
    static const char digits[] = "0123456789abcdef";
    char buf[2 + 16];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 16; i++) {
        buf[2 + i] = digits[(v >> (60 - 4 * i)) & 0xful];
    }
    int start = 2;
    while (start < 2 + 15 && buf[start] == '0') start++;   /* 至少留最后一位 */
    put_mem(buf, 2);                        /* "0x" */
    put_mem(buf + start, (size_t)(2 + 16 - start));
}

/* 十进制，支持负数：SIGABRT 的 si_code 是 SI_TKILL = -6，会走到这里 */
static void put_dec(long v)
{
    char rev[24];
    char out[24];
    int n = 0;
    unsigned long uv;

    if (v < 0) {
        put_mem("-", 1);
        uv = (unsigned long)(-(v + 1)) + 1UL;   /* 这样写才不会在 LONG_MIN 上溢出 */
    } else {
        uv = (unsigned long)v;
    }
    if (uv == 0) rev[n++] = '0';
    while (uv > 0) {
        rev[n++] = (char)('0' + (int)(uv % 10UL));
        uv /= 10UL;
    }
    for (int i = 0; i < n; i++) out[i] = rev[n - 1 - i];
    put_mem(out, (size_t)n);
}

static const char *sig_name(int sig)
{
    switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGABRT: return "SIGABRT";
    case SIGBUS:  return "SIGBUS";
    case SIGFPE:  return "SIGFPE";
    case SIGILL:  return "SIGILL";
    default:      return "SIG?";
    }
}

/* si_code 的名字只在 SIGSEGV 上有普适含义（SEGV_MAPERR / SEGV_ACCERR） */
static const char *sig_code_name(int sig, int code)
{
    if (sig == SIGSEGV) {
        if (code == SEGV_MAPERR) return "SEGV_MAPERR";   /* 地址压根没映射：野指针 / NULL */
        if (code == SEGV_ACCERR) return "SEGV_ACCERR";   /* 映射了但没权限：写只读段 */
        return NULL;
    }
    if (sig == SIGBUS) {
        if (code == BUS_ADRALN) return "BUS_ADRALN";     /* 对齐错误：ARM 上常见 */
        if (code == BUS_ADRERR) return "BUS_ADRERR";
        return NULL;
    }
    return NULL;
}

/* ---------- handler 本体 ---------- */

/* 不是 static：`-rdynamic` 只导出全局符号，写成 static 的话 handler 自己那一帧
 * 在日志里只剩 `(+0x19d0)` 没有名字（2026-10-29 实测踩过）。 */
void on_crash(int sig, siginfo_t *si, void *uctx)
{
    (void)uctx;   /* ucontext 里有寄存器现场，MVP 不用（core dump 才需要） */

    put_str("\n=== CRASH === pid=");
    put_dec((long)getpid());
    put_str(" tid=");
    put_dec((long)syscall(SYS_gettid));   /* 多线程时必须知道是哪个线程崩的 */

    put_str("\nsignal=");
    put_str(sig_name(sig));
    put_str(" (");
    put_dec((long)sig);
    put_str(")\n");

    int code = (si != NULL) ? si->si_code : 0;
    put_str("si_code=");
    put_dec((long)code);
    const char *cn = sig_code_name(sig, code);
    if (cn != NULL) {
        put_str(" (");
        put_str(cn);
        put_str(")");
    }

    /* si_addr 只有 SIGSEGV/SIGBUS 上有意义；SIGABRT 上是个垃圾值，别打出来误导人 */
    if (sig == SIGSEGV || sig == SIGBUS) {
        put_str("\nsi_addr=");
        put_hex((si != NULL) ? (unsigned long)si->si_addr : 0UL);
    }
    put_str("\n");

    /* ---------- 栈回溯（2026-10-29 学生选 A：现场直出函数名） ----------
     * backtrace 只负责把返回地址填进数组（不 malloc）；
     * backtrace_symbols_fd 直接把"地址→符号名"写进 fd（也不 malloc）。
     * 不能用 backtrace_symbols：它返回 char**，是 malloc 出来的，handler 里不许。 */
    void *frames[CRASH_BT_DEPTH];
    int nf = backtrace(frames, CRASH_BT_DEPTH);
    put_str("backtrace(n=");
    put_dec((long)nf);
    put_str("):\n");
    if (nf > 0) {
        backtrace_symbols_fd(frames, nf, g_fd);   /* 每层一行：./bin/xxx(func+0x14) [0x...] */
    }
    put_str("=== END ===\n");

    /* 用 _exit 而不是 exit：
     *   exit 会跑 atexit 回调、刷 stdio 缓冲 —— 这些都要拿锁，而崩溃的那个线程
     *   很可能正握着那把锁 → 在 handler 里再死一次，或者干脆挂住不退出。 */
    _exit(128 + sig);
}

/* ---------- 对外：装 handler ---------- */

int crash_install(const char *path)
{
    if (path == NULL) path = "/tmp/gateway-crash.log";

    /* O_APPEND：多线程同时崩时各自追加，不会互相覆盖
     * O_CLOEXEC：常驻 fd 的代价要管住 —— 别漏给 exec 出来的子进程 */
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd < 0) return -1;
    g_fd = fd;

    /* 启动先记一笔：验收时一眼就能分辨"这行是这次运行写的"还是上次的残留 */
    put_str("--- crash logger armed: pid=");
    put_dec((long)getpid());
    put_str(" path=");
    put_str(path);
    put_str(" ---\n");

    /* 记装载基址。dladdr 不在 async-signal-safe 清单里，但这里不在信号上下文 ——
     * 是启动期普通代码，随便调。放在 handler 里就成了真正的坑。 */
    Dl_info info;
    if (dladdr((void *)crash_install, &info) != 0 && info.dli_fbase != NULL) {
        g_base = (unsigned long)info.dli_fbase;
    }
    put_str("base=");
    put_hex(g_base);
    put_str("\n");

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_crash;     /* 配 SA_SIGINFO 才拿得到 si_code / si_addr */
    sigemptyset(&sa.sa_mask);
    /* SA_SIGINFO ：handler 要三参数版本
     * SA_RESETHAND：handler 自己再崩就恢复默认动作直接死，不会无限重进 handler */
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;

    static const int sigs[] = { SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL };
    for (size_t i = 0; i < sizeof sigs / sizeof sigs[0]; i++) {
        if (sigaction(sigs[i], &sa, NULL) < 0) return -1;
    }
    return 0;
}

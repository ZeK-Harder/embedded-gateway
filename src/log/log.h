/* src/log/log.h —— 分级日志模块的对外合同
 *
 * 这个头文件就是"别人怎么用日志"的全部说明。
 * 业务代码只 #include 它，永远不直接碰 log.c 里的任何变量。
 */
#ifndef LOG_H          /* 头文件保护：和 proto.h 里你用的同一套写法 */
#define LOG_H

/* ---------- 等级定义 ---------- */
/* 约定：数值越大 = 越严重。全模块（过滤、滚动、落盘）都依赖这个顺序。
 *   最低级：LOG_DEBUG  —— 只在你调试时想看的东西（收到的字节数、连接 fd 等）
 *   正常：  LOG_INFO   —— 程序生命周期里值得记一笔的事（启动、模块初始化完成）
 *   可疑：  LOG_WARN   —— 还没坏，但要留意（CRC 校验失败、包被丢弃）
 *   出错：  LOG_ERROR  —— 明确的错误（写数据库失败、连接异常断开）
 */
typedef enum {
    LOG_LV_DEBUG = 0,
    LOG_LV_INFO  = 1,
    LOG_LV_WARN  = 2,
    LOG_LV_ERROR = 3
} log_level_t;

/* 多加一个参数：门槛。低于它的日志一律不写。
 * 比如传 LOG_LV_INFO，则所有 LOG_DEBUG 调用被静默丢弃。 */
int log_init(const char *path, log_level_t min_level);

/* 收尾：把缓冲里没落盘的东西刷出去，然后关闭文件。
 * 程序正常退出前必须调用它，否则最后几行会丢。 */
void log_close(void);

/* 参数说明：
 *   lv    —— 等级
 *   file  —— 源文件名（由调用方传 __FILE__）
 *   line  —— 行号（由调用方传 __LINE__）
 *   fmt   —— 格式串，和 printf 一个规矩，如 "dev %d timeout"
 *   ...   —— 与 fmt 里的 % 一一对应的可变参数
 * 注意：参数名从 msg 改成了 fmt —— 它现在是"待格式化的模板"，不再是"一个现成的字符串"。名字要跟着语义走。
 */
void log_write(log_level_t lv, const char *file, int line, const char *fmt, ...);

#define LOG_DEBUG(fmt, ...)  log_write(LOG_LV_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)   log_write(LOG_LV_INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)   log_write(LOG_LV_WARN, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...)  log_write(LOG_LV_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#endif /* LOG_H */
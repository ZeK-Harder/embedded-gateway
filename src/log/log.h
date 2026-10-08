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
 *   msg   —— 消息
 * 警告：这是一次接口变更，所有调用点必须同步修改，否则编译不过。 */
void log_write(log_level_t lv, const char *file, int line, const char *msg);

/* ---------- 业务代码只用这四个宏，不要直接调 log_write ---------- */
/* 样板：LOG_DEBUG("hi") 会被原地替换成
 *   log_write(LOG_LV_DEBUG, __FILE__, __LINE__, "hi")
 * 所以调用点再也不用手敲 __FILE__ / __LINE__ 这两坨。
 * 行尾没有分号（分号由调用方写）。 */
#define LOG_DEBUG(msg)  log_write(LOG_LV_DEBUG, __FILE__, __LINE__, msg)
#define LOG_INFO(msg)   log_write(LOG_LV_INFO, __FILE__, __LINE__, msg)
#define LOG_WARN(msg)   log_write(LOG_LV_WARN, __FILE__, __LINE__, msg)
#define LOG_ERROR(msg)  log_write(LOG_LV_ERROR, __FILE__, __LINE__, msg)

#endif /* LOG_H */
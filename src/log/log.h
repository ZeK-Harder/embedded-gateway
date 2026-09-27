/* src/log/log.h —— 分级日志模块的对外合同
 *
 * 这个头文件就是"别人怎么用日志"的全部说明。
 * 业务代码只 #include 它，永远不直接碰 log.c 里的任何变量。
 */
#ifndef LOG_H          /* 头文件保护：和 proto.h 里你用的同一套写法 */
#define LOG_H

/* ---------- 等级定义 ---------- *
 * 约定：数值越大 = 越严重。全模块（过滤、滚动、落盘）都依赖这个顺序。
 * TODO(你): 补全 4 个成员的名字和值。
 *   最低级：LOG_DEBUG  —— 只在你调试时想看的东西（收到的字节数、连接 fd 等）
 *   正常：  LOG_INFO   —— 程序生命周期里值得记一笔的事（启动、模块初始化完成）
 *   可疑：  LOG_WARN   —— 还没坏，但要留意（CRC 校验失败、包被丢弃）
 *   出错：  LOG_ERROR  —— 明确的错误（写数据库失败、连接异常断开）
 */
typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO  = 1,
    LOG_WARN  = 2,
    LOG_ERROR = 3
} log_level_t;
/* ---------- 对外函数（这一步先要这三个，后面再增） ---------- */

/* 多加一个参数：门槛。低于它的日志一律不写。
 * 比如传 LOG_INFO，则所有 LOG_DEBUG 调用被静默丢弃。 */
int log_init(const char *path, log_level_t min_level);

/* 收尾：把缓冲里没落盘的东西刷出去，然后关闭文件。
 * 程序正常退出前必须调用它，否则最后几行会丢。 */
void log_close(void);

/* 最朴素的写：原样把 msg 写进日志文件（自己会补换行）。
 * 注意：这一步故意做得很弱——只能写一个完整字符串，不能带 %d %s。
 * 第一个参数换成等级。调用方现在必须"声明这条日志多重"。 */
void log_write(log_level_t lv, const char *msg);

#endif /* LOG_H */
# 概念卡 07 · SQLite 落库：连接、预编译之外的写法与错因分类

> 归属：store 模块收尾卡（2026-10-29）。读法：先看"是什么 / 为什么"，再对照"我代码里的位置"，最后自己合上讲两句话。

## 1. 是什么

**SQLite 是一个把整个数据库放进一个文件的库**，不需要服务端进程。你的程序通过一个 `sqlite3*` 连接句柄跟它打交道：`sqlite3_open` 拿句柄，`sqlite3_exec` 送一条 SQL，`sqlite3_close` 还句柄。数据落在哪个文件由 `sqlite3_open` 的路径决定；文件不存在时它会新建（这一步成功不代表库能用，表是后面 `CREATE TABLE` 建的）。

## 2. 为什么这么设计

- **为什么 IO 线程不碰库**：`sqlite3_exec` 会落盘，慢的时候是毫秒级甚至更长；事件循环里做这件事，整个网关的收发就停了。所以帧走 `pool_submit` 进 worker，worker 里落库，IO 线程只负责收发字节。
- **为什么全进程只用一个连接**：SQLite 默认以 serialized 模式编译（`THREADSAFE=1`），同一个连接被多线程调用时内部自己加锁，安全但串行；V1 的写入量（每帧一条）用不上多连接。
- **为什么必须按返回值分类**：错因不同，调用方的动作也不同。写成一码 `-1`，上层只能一律丢帧——磁盘满了你也看不出来。

## 3. 我代码里的位置

| 位置 | 干什么 |
| --- | --- |
| `src/store/store.c` · `store_open` / `store_close` | 拿句柄 / 还句柄；失败时 db 可能仍非空，必须先 close 再返回 |
| `src/store/store.c` · `store_init_schema` | 两条 `CREATE TABLE IF NOT EXISTS` + 一条 `CREATE INDEX`；幂等靠 `IF NOT EXISTS` |
| `src/store/store.c` · `store_insert_event` | `snprintf` 拼 INSERT → `sqlite3_exec` → 按 `rc` 返回 0 / -1 / -2 / -3 |
| `src/store/store.c` · `store_query_recent` + `row_adapter` | 查询用回调逐行交付；`exec` 给的 `vals[i]` 是字符串，要 `atoi/atoll` 转回数字 |
| `src/net/epoll_server.c` · `frame_job_run` | worker 解码成功后调 `store_insert_event`；落库在第一行 send 回投之前 |

## 4. 易错点

1. **`snprintf` 少参数**：`snprintf(缓冲, 大小, 格式, 参数…)` 四个位置缺一不可；只给格式串编译就过不去。
2. **SQL 里写变量名而不是值**：`WHERE dev_id = dev_id` 是"这一列等于它自己"，恒真 → 全表被选中；要填值必须 `%d` + 参数。实测：3 行全部命中，而 `dev_id = 2` 只命中 1 行。
3. **`CREATE TABLE` 不带列定义**：`CREATE TABLE IF NOT EXISTS devices` 报 `incomplete input`，必须写 `( 列 类型 约束 )`。
4. **`IF NOT EXISTS` 忘了加**：第二次启动直接报"表已存在"，服务起不来。
5. **约束错误当成致命错误**：`SQLITE_CONSTRAINT(19)` 说明这条数据不合规（比如 `dev_id=300` 撞 `CHECK (dev_id BETWEEN 0 AND 255)`），应该丢这一帧继续，而不是让进程退出；脏数据本来就没进库。
6. **`SQLITE_BUSY(5)` 自己傻等**：默认没有 busy handler，等不到就返回；要重试得由上层决定（`sqlite3_busy_timeout` 是升级项）。
7. **错误串的来源**：`sqlite3_errmsg(db)` 拿当前连接最近一次错误，不用 free；`sqlite3_exec` 的 `errmsg` 出参是 malloc 出来的，必须 `sqlite3_free`——所以这次一律传 `NULL`。

## 5. 面试会怎么追问

- "你的网关收到 400 帧/秒，每帧都要落库，谁在写库？" → worker，按连接分片；IO 线程只收发。
- "磁盘满了会怎样？" → `SQLITE_FULL(13)`，`store_insert_event` 返回 -3，上层报警；V1 只报不停写（这是已知边界）。
- "两个 worker 同时写同一个库文件会坏吗？" → 不会，serialized 模式内部加锁；但会串行，写入量大时要考虑 WAL 或单写线程（升级项）。
- "为什么不用 prepared statement？" → V1 拼串够用且好读；数据量上来要换成 `sqlite3_prepare_v2`/`bind`/`step`（避免反复解析 SQL，也避免拼串注入）。

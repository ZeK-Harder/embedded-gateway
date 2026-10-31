# embedded-gateway

工业网关主线代码库。零件来源：https://github.com/ZeK-Harder/embedded-linux

## 是什么

常驻工控机的多线程网络服务端：接收车间设备（温控器 / 电表 / PLC）经以太网上报的
自定义协议报文，做四件事——①维护设备连接（谁在线、谁掉线）；②解析上报数据（校验、
拆包、解转义）；③存入本地数据库供事后查询；④记录一切异常。

没有 Web 界面、没有云、没有 UI——这就是交付物的全部功能。

## 路线

| 版本 | 内容 | 阶段 |
| --- | --- | --- |
| V1.0（进行中） | 协议帧 / epoll ET / 线程池 / SQLite / 分级日志 / 崩溃回溯 / 100 并发压测 | 阶段一 |
| V2.0 | 心跳 · 断线重连 · 黑名单 · 极简 HTTP；CMake + 单测 + 异常注入 | 阶段二 |
| ARM 移植 | 交叉编译 + IMX6ULL 部署 + systemd + gdbserver | 阶段三 |

## 边界（V1 明确不做，写入 roadmap）

心跳 / 重连 / 拉黑 / HTTP（属 V2）、鉴权、加密、多进程、多机部署。

## 目录约定

    src/proto  net  pool  store  log   # 模块分目录，纯逻辑与 IO 胶水分开
    tools/loadgen/                     # 压测客户端
    docs/                              # protocol / architecture / test-report / retro / 概念卡
    scripts/                           # build.sh  run.sh  bench.sh
    bin/                               # 构建产物统一放这里（已 gitignore）

## 构建

依赖：`gcc` + `pthread` + `libsqlite3-dev`（Ubuntu：`sudo apt install build-essential libsqlite3-dev`）

```bash
make          # 一条命令产出 10 个目标，含 bin/gateway 与 bin/loadgen
make test     # 5 个纯逻辑单测（proto 四个 + 帧解析）；任一失败 → 退出码非 0
make clean
```

`-Wall -Wextra -Wshadow -std=c11`，**零警告**。构建产物统一进 `bin/`（已 gitignore）。

## 运行

```bash
# 终端 A：网关（端口写死 8090；库路径可传，默认 /tmp/gateway.db）
./bin/gateway /tmp/gateway.db

# 终端 B：8 条连接 × 5 秒 × 每 50ms 一帧
./bin/loadgen -c 8 -t 5 -i 50
```

`Ctrl-C`（SIGINT）或 SIGTERM 优雅退出：先把批量事务里攒的那一批提交掉，再 close eventfd / listen / epoll，退出码 0。

| 程序 | 参数 | 说明 |
| --- | --- | --- |
| `bin/gateway` | `argv[1]` | SQLite 库路径，默认 `/tmp/gateway.db` |
| | 环境变量 `GATEWAY_CRASH_LOG` | 崩溃日志路径，默认 `/tmp/gateway-crash.log`；**打不开就启动失败** |
| `bin/loadgen` | `-c 1..255` | 并发连接数（dev_id 是 1 字节） |
| | `-t 1..3600` | 压测时长（秒） |
| | `-i 1..60000` | 每帧间隔（毫秒） |
| | `-h host[:port]` / `-p port` | 目标地址，默认 `127.0.0.1:8090` |
| | `-m framewise\|sticky` | `framewise`（默认）一帧一次 send；`sticky` 每轮发 2.5 帧，专门造粘包 |

## 压测摘要（2026-10-31，WSL Ubuntu 22.04 / gcc 11.4.0）

`./bin/loadgen -c 50 -t 600 -i 50`，脚本从 `/proc/<pid>/status` 外部采 RSS：

| 指标 | 数值 |
| --- | --- |
| 发送 / 接收 / 服务端处理 | 600100 / 600100 / 600100 帧 |
| 错误率 | **0.000%**（failed=0，drop 行 0） |
| 吞吐 | **996 帧/秒** |
| RSS | 3452 KB → 11504 KB（斜率衰减：0–30s ≈5280、30–120s ≈2096、120–600s ≈285 KB/分钟） |
| 线程数 | 5（压测当时；信号线程 10-31 晚加入后为 6） |
| fd 峰值 | 59 |

**同负载改前是 38.6% 错误率**，根因不是网络而是 SQLite：逐帧一个事务 ⇒ 每帧一次 `fsync`。
用「同一份负载把库换成 tmpfs（`/dev/shm`）」对照实验把瓶颈钉死（38.596% → 0.000%），
改成批量事务（`STORE_BATCH_MAX 64` / `STORE_BATCH_MS 50`）后错误率归零。
代价：进程崩溃 / 断电最多丢「还没 COMMIT 的那一批」（≤64 帧或 ≤50ms）。

## 已知边界（V1 故意不做，不是忘了）

- **写缓冲满 → 整帧丢弃**（`wbuf` 8192 B）：帧边界比"少一帧"重要，绝不写半个帧；真正的背压属 V2
- **任务队列满 → 整帧丢弃**（每条队列 65536 格）
- **连接槽位 1024 满 → 直接 close 新连接**，不打日志（可观测性缺口，已知）
- **端口写死 8090**，没有 `-p` 参数
- 心跳 / 断线重连 / 黑名单 / 鉴权 / 加密 / 多进程 → 全属 V2
- 恶意报文只做了 2 类（CRC 错帧、截断帧）；其余 3 类属寒假
- 崩溃日志给的是「基址 + 偏移」，符号还原靠 `addr2line`（保留 PIE/ASLR）
- **`src/log/` 没接进构建**：Makefile 当前不编译它，网关日志用的是裸 `printf()`；
  log 的分级/调用点/线程安全在 2026-10-18 单独验过（`23eb4af`），但把它接进网关属 V2 组装

## 规范

- 提交信息：`<type>: <一句话>`（feat / fix / docs / chore / test）
- 每学一个概念写一张概念卡进 `docs/概念卡/`
- 验收对齐《项目要求》项目 A 的 8 条，收尾时逐条打勾

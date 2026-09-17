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

TODO：第一个可编译版本出现后补。

## 规范

- 提交信息：`<type>: <一句话>`（feat / fix / docs / chore / test）
- 每学一个概念写一张概念卡进 `docs/概念卡/`
- 验收对齐《项目要求》项目 A 的 7 条，收尾时逐条打勾

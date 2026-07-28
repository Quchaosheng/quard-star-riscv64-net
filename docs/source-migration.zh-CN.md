# 源码迁移

[English](source-migration.md) | **简体中文**

本仓库使用全新的 Git 历史。第一方代码从不可变的固定源码版本中选择性迁移，
不导入旧仓库历史，也不复制旧仓库捆绑的第三方源码树。

截至 2026-07-23，原始源码仓库已不再公开。下列仓库名和固定版本仅作为历史
迁移记录保留，目前无法从公开项目中独立获取这些版本。

| 来源 | 固定版本 | 纳入的源码 |
| --- | --- | --- |
| `Quchaosheng/quard-star-riscv64-kernel` | `641f42560999ab00ad7ba01169cb2b3d723d8c48` | 启动、DTS、内核、可信 domain 平台代码，以及 quard-star QEMU/OpenSBI 修改 |
| `Quchaosheng/tiny-tcpip-stack` | `32e4988e2d482ad3ee406e36b5adbd84a63c8e9e` | `code/pc/src/net/net`、`code/pc/src/net/src` 和选定的 `code/pc/src/app` 模块 |

TCP/IP 协议核心只从 `code/pc/src/net` 迁移。较旧的 `code/src/net`、
`code/x86os-with-net` 和 `chapter` 目录不作为协议核心来源。

内核和 TCP/IP 协议栈是项目作者拥有的第一方实现。QEMU、OpenSBI、
FreeRTOS-Kernel、dtc/libfdt、FatFs 和 nanoprintf 仍是按上游许可证使用的
第三方组件；详见 [THIRD_PARTY.md](../THIRD_PARTY.md)。

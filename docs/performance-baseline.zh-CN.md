# 性能基线

[English](performance-baseline.md) | **简体中文**

性能工作从可重复的验收负载开始。这些报告是观测结果，不是 CI 门槛；不同
主机、QEMU 构建或 runner 类型产生的数据不得直接比较。

## 工作负载

### M8 集成测试

M8 启动 7 个普通 hart 和 1 个可信 FreeRTOS hart，执行存储与网络应用链，
通过 TFTP 传输 1 MiB 文件，并检查双向 PMP 拒绝访问。对端耗时覆盖完整集成
验收路径，而不是单个协议操作。

报告工具会校验普通 QEMU 日志和 TAP 对端计数器，但不会读取 `trusted.log`；
可信调度和可信侧 PMP 标记仍由完整的 `m8-smoke.sh` 验收负责。

```sh
./scripts/prepare-fatfs.sh
make m8-build
sudo -v
sudo -E make m8-smoke
commit=$(git rev-parse HEAD)
python3 scripts/perf-baseline.py \
  --stage m8 --commit "$commit" \
  --qemu-log out/m8/qemu.log \
  --peer-stats out/m8/m5-peer.stats \
  --json-out out/performance/m8.json \
  --markdown-out out/performance/m8.md
```

`m8-smoke.sh` 会修改生成的 FatFs 镜像。每个 M8 样本前都要重新运行
`make m8-build`，保证测量从新磁盘开始。重复使用 `out/m8/disk/disk.img` 得到
的数据不能与新磁盘基线比较。

在 WSL2 中使用由 Windows 创建的 Git worktree 时，应执行
`git.exe rev-parse HEAD | tr -d '\r'` 获取提交号，因为 worktree 元数据包含
Windows 路径。

### M6C2 累计压力测试

压力负载执行 100000 次分配器操作、10000 次迁移、8 个并行 TCP 服务连接和
100 次重连。完成时必须有 108 次握手、echo 和 FIN，且没有存活或未完成连接。

```sh
sudo -E make m6c2-stress
commit=$(git rev-parse HEAD)
python3 scripts/perf-baseline.py \
  --stage m6c2-stress --commit "$commit" \
  --qemu-log out/m6c2-stress/qemu.log \
  --peer-stats out/m6c2-stress/m5-peer.stats \
  --json-out out/performance/m6c2-stress.json \
  --markdown-out out/performance/m6c2-stress.md
```

## 指标

| 指标 | 来源 | 含义 |
| --- | --- | --- |
| Guest 耗时 tick | `QS:STRESS_ELAPSED_TICKS` | 从自测开始到最后门槛的 Guest `mtime` tick |
| Host 耗时秒数 | `m5-peer.stats` | TAP 对端观测的墙钟时间 |
| 分配操作 | QEMU 日志 | 普通 hart 上完成的分配器压力操作 |
| 迁移 | QEMU 日志 | 完成的跨 hart 调度迁移 |
| TFTP 字节数 | 对端统计 | 1 MiB 传输接受的应用负载 |
| TCP 压力计数 | 对端统计 | 已完成连接、峰值并发、重连和清理状态 |

`tftp_bytes_per_second` 只用 TFTP 字节数除以对端耗时得到。它描述完整 M8
对端区间，不是线速 TFTP 吞吐量。

## 比较规则

1. 只比较使用相同主机、WSL 发行版、QEMU 构建、命令和负载配置的重复运行。
2. 随结果保留原始 `qemu.log`、`m5-peer.stats` 和生成的 JSON。
3. 至少运行 3 个样本后才能声称性能改善，并报告每个值，而非只报告最好值。
4. GitHub 托管 runner 的数据仅作为执行证据，不作为回归阈值。
5. 不得将 QEMU 结果描述为物理开发板性能。

## 初始观测

首批观测于 2026-07-23 在同一台 Windows 11 主机的 WSL2 Ubuntu 24.04 中
采集。它们只建立可追溯起点；单个样本不能证明优化有效。

| 阶段 | 提交 | Guest 耗时 tick | Host 耗时秒数 | 阶段证据 |
| --- | --- | ---: | ---: | --- |
| M8 | `7eeadb1e9689732d96a3474089966b1041559ae0` | 264957814 | 30.02252233500002 | 报告工具验证 14000 次分配、100 次迁移、1 MiB TFTP 和零未完成数据包；完整冒烟测试另行通过可信/PMP 检查 |
| M6C2 stress | `cfb695f6a4940769d01ae1c7e331c71fd530610a` | 3225305239 | 245.72794389 | 100000 次分配、10000 次迁移、108 次 TCP 交换、峰值 8、100 次重连，零存活/未完成连接 |

M8 集成区间推导出的 TFTP 应用负载速率为每秒 34926.31 字节。该区间包含启动
和其他验收操作，不得将其表示为独立网络吞吐量。更早的本地产物仍可作为验收
证据，但不能在事后归属于某个提交。

## 可重复性观测

在提交 `080a9dd40de6a8ff1aae66bfa80a8bb315e39f6d` 上，每种负载各采集 3 个
新样本，环境仍是同一台 Windows 11 主机上的 WSL2 Ubuntu 24.04。这些测量用于
描述当前基线，不证明存在优化。

主机描述和新磁盘流程是操作员记录的元数据。当前 JSON schema 会校验提交号、
负载计数器、通过标记和耗时值，但不编码或独立验证主机身份、QEMU 二进制摘要、
构建命令或初始磁盘摘要。报告工具只验证提交号格式，不证明日志由该提交生成。

| 阶段 | 样本 | Guest 耗时 tick | Host 耗时秒数 |
| --- | ---: | ---: | ---: |
| M8 | 1 | 248036291 | 31.84657113899999 |
| M8 | 2 | 258244754 | 29.602071138 |
| M8 | 3 | 254639209 | 28.443526686 |
| M6C2 stress | 1 | 3229017703 | 249.91188124800001 |
| M6C2 stress | 2 | 3222401076 | 249.816960978 |
| M6C2 stress | 3 | 3220955776 | 247.64832866899997 |

相对离散度计算公式为 `(最大值 - 最小值) / 中位数 * 100`。

| 阶段 | 指标 | 最小值 | 中位数 | 最大值 | 相对离散度 |
| --- | --- | ---: | ---: | ---: | ---: |
| M8 | Guest 耗时 tick | 248036291 | 254639209 | 258244754 | 4.009% |
| M8 | Host 耗时秒数 | 28.443526686 | 29.602071138 | 31.84657113899999 | 11.496% |
| M6C2 stress | Guest 耗时 tick | 3220955776 | 3222401076 | 3229017703 | 0.250% |
| M6C2 stress | Host 耗时秒数 | 247.64832866899997 | 249.816960978 | 249.91188124800001 | 0.906% |

按记录的运行流程，所有纳入统计的 M8 样本都在冒烟测试前重新生成了磁盘。
更早的一次尝试复用了前一轮磁盘，导致 M7E TFTP RRQ 验收失败；该尝试与新
磁盘基线不可比，因此未纳入上述性能样本。

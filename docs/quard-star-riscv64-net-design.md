# quard-star-riscv64-net 设计与实施说明

> 状态：设计基线已确认，尚未开始实现
> 目标平台：Windows 11 + WSL2 Ubuntu 24.04/26.04 LTS + QEMU RISC-V64
> 项目定位：从零构建的 RISC-V64 SMP 教学内核，融合自研 TCP/IP 协议栈、VirtIO 网络、用户态 Socket API，以及独立的 FreeRTOS trusted domain

## 1. 文档目的

本文档是新项目的唯一实施基线，说明：

- 新项目最终要实现什么。
- 哪些旧代码保留、迁移、重写或删除。
- 新仓库应采用什么目录结构。
- 如何在 Windows 环境中稳定开发和调试。
- 每个阶段的交付物、验收标准和测试方式。
- 哪些优化首版不做，避免重复造轮子和过早优化。
- 如何确保新仓库不包含旧 Git 作者和旧账号记录。

实现过程中如果需要改变本文档中的架构或范围，应先更新文档，再修改代码。

## 2. 项目目标

### 2.1 最终目标

构建一个可在 QEMU 中运行的 RISC-V64 多核操作系统实验平台，具备以下能力：

1. 使用 OpenSBI 启动和管理多个 hart。
2. hart 0-6 运行 RISC-V64 SMP 内核。
3. hart 7 运行独立的 FreeRTOS trusted domain。
4. 内核支持 Sv39 虚拟内存、进程、抢占式调度、系统调用和用户态程序。
5. 使用公共 VirtIO ring 层同时支持 VirtIO block 和 VirtIO net。
6. QEMU 通过 TAP 与宿主环境建立二层网络连接。
7. 将现有自研 TCP/IP 协议栈移植到 RISC-V 内核。
8. 支持 Ethernet、ARP、IPv4、ICMP、UDP、TCP、DNS 和 Loopback。
9. 提供用户态 Socket API。
10. 提供 Ping、UDP/TCP Echo、TFTP、HTTP 和 NTP 示例程序。
11. 从空环境完成依赖获取、构建、启动和自动化测试。
12. 使用全新的 Git 历史，所有新提交只属于新账号 `Quchaosheng`。

### 2.2 第一阶段 SMP 范围

- 首先只启动 hart 0 和 hart 1。
- 所有 CPU 数据结构从开始就支持 hart 0-6。
- 双 hart 稳定后，再逐步启用 hart 2-6。
- hart 7 始终属于 trusted FreeRTOS domain，不访问普通内核内部数据。

### 2.3 项目非目标

首版明确不做：

- 不把 Windows、STM32 和旧 x86 OS 平台代码整体复制到新仓库。
- 不引入 lwIP、smoltcp 等第二套 TCP/IP 协议栈。
- 不实现 VirtIO multiqueue、RSS 或每核网络协议栈。
- 不在首版实现 IPv6。
- 不在首版实现 TLS/HTTPS。
- 不在首版实现完整 POSIX 兼容层。
- 不在首版实现复杂的每核调度队列和负载均衡。
- 不为了展示项目数量保留重复源码和生成物。

## 3. 核心设计原则

### 3.1 优先复用

不重复实现已经存在并经过验证的能力：

- SBI 启动、HSM 和 domain：复用 OpenSBI。
- 设备树解析：复用 `libfdt`。
- trusted domain 调度：复用 FreeRTOS-Kernel。
- 原子操作：复用 C11 原子或 GCC `__atomic` 内建函数。
- TCP/IP 协议、Socket、内存池和定时器：复用自研协议栈现有实现。
- TAP、网络命名空间、抓包和网络验证：复用 Linux 原生工具。
- 文件系统：复用 FatFs，不重新实现 FAT。
- 格式化输出：复用 nanoprintf，不维护存在边界问题的自制 `printf`。

### 3.2 先正确，再优化

首版采用：

- 一个带锁的全局运行队列。
- 一个 TCP/IP 核心线程。
- 一组 VirtIO-net RX/TX queue。
- 固定容量的协议栈内存池。
- 明确、简单、可测试的锁顺序。

只有性能测试证明存在瓶颈后，才考虑每核运行队列、VirtIO multiqueue、零拷贝、RSS 或协议栈分片。

### 3.3 第三方代码与自有代码分离

有正式 Git 上游的第三方项目使用固定提交的 Git submodule。只提供正式发布归档的上游使用固定版本、下载 URL 和 SHA-256 校验，自有修改以补丁或独立平台文件保存。FatFs 是首版唯一的归档依赖例外。

### 3.4 自有源码迁移基线

迁移只使用下表指定的提交和目录，不从旧仓库的重复副本中人工拼装：

| 来源 | 固定提交 | 源目录 | 目标 | 处理方式 |
|---|---|---|---|---|
| `Quchaosheng/quard-star-riscv64-kernel` | `641f42560999ab00ad7ba01169cb2b3d723d8c48` | `boot/`、`dts/`、`os/` | `platform/quard-star/`、`kernel/`、`user/` | 迁移自有代码并按新结构整理 |
| `Quchaosheng/quard-star-riscv64-kernel` | 同上 | `qemu-8.0.2/hw/riscv/quard_star.c`、相关注册文件和头文件 | `patches/qemu/` | 与官方 QEMU `v8.0.2` 对比后生成最小补丁 |
| `Quchaosheng/quard-star-riscv64-kernel` | 同上 | `opensbi-1.2/platform/quard_star/` | `patches/opensbi/` | 生成最小补丁，修复后适配官方 OpenSBI `v1.2` |
| `Quchaosheng/quard-star-riscv64-kernel` | 同上 | `trusted_domain/` | `trusted/` | 只迁移自有平台层和示例；FreeRTOS 内核改用官方 submodule |
| `Quchaosheng/tiny-tcpip-stack` | `32e4988e2d482ad3ee406e36b5adbd84a63c8e9e` | `code/pc/src/net/net/`、`code/pc/src/net/src/` | `kernel/net/include/`、`kernel/net/core/` | 唯一协议栈核心基线 |
| `Quchaosheng/tiny-tcpip-stack` | 同上 | `code/pc/src/app/` | `user/apps/` | 按里程碑逐个迁移应用 |

禁止从 `code/src/net/`、`code/x86os-with-net/` 或 `chapter/` 补取同名协议实现。确实需要旧版本中的修复时，先形成独立差异说明和测试，再移植到唯一基线。

### 3.5 版权与致谢

- 新内核是参考 rCore 教学思路后独立设计和重构的 C 实现，按自有 MIT 代码管理。
- 根目录 `README.md` 在致谢章节说明 rCore 对教学思路的启发；致谢不表示代码依赖或派生关系。
- `tiny-tcpip-stack` 迁移代码按自有 MIT 代码管理，并在 `README.md` 和迁移记录中注明来源仓库与固定提交。
- QEMU、OpenSBI、FreeRTOS-Kernel、FatFs、dtc/libfdt 和 nanoprintf 继续遵守各自许可证，不能用根目录 MIT License 覆盖。

## 4. 总体架构

### 4.1 CPU 和 domain

```text
QEMU: 8 harts

hart 0-6
  OpenSBI untrusted-domain
  RISC-V64 SMP kernel
  user processes
  VirtIO block/net
  TCP/IP stack

hart 7
  OpenSBI trusted-domain
  FreeRTOS
  trusted demo/services
```

内核不能写死“当前有 7 个 hart”。内核从传入的 kernel DTB 枚举本 domain 可见的 hart，使用 SBI HSM 查询和启动指定 hart，维护 possible、present、started 和 online 状态。SBI 不提供 hart 枚举接口。

### 4.2 板级资源和 domain 契约

首版固定使用以下资源分配；`platform/quard-star/include/layout.h` 是地址和 IRQ 的唯一常量来源，QEMU 板级模型、OpenSBI domain DTB、kernel DTB、链接脚本和驱动必须包含该文件或通过测试与它逐项比对：

| 资源 | 地址/范围 | IRQ | 所属 |
|---|---|---|---|
| DRAM | `0x80000000-0xBFFFFFFF` | - | 总物理内存 1 GiB |
| 普通内核 DRAM | `0x80000000-0xBF7FFFFF` | - | untrusted domain |
| trusted DRAM | `0xBF800000-0xBFFFFFFF` | - | trusted domain，8 MiB |
| UART0 | `0x10000000` | 10 | 普通内核日志 |
| UART1 | `0x10001000` | 11 | 保留调试串口 |
| UART2 | `0x10002000` | 12 | trusted domain 日志 |
| RTC | `0x10003000` | 13 | 普通内核 |
| VirtIO block MMIO | `0x10100000` | 1 | 普通内核 |
| VirtIO net MMIO | `0x10101000` | 2 | 普通内核 |
| QEMU test/exit | `0x00100000` | - | 普通内核测试退出 |
| PLIC | `0x0C000000` | - | 按 domain 和 hart context 授权 |
| CLINT/ACLINT | `0x02000000` | - | OpenSBI 管理，S-mode 通过 SBI 使用 timer/IPI |

trusted DRAM 使用一个 8 MiB 对齐的 OpenSBI memregion，`base = 0xBF800000`、`order = 23`，不再拆成多个容易留下空洞的小区域。

首版采用双向隔离：

- untrusted domain 可访问普通内核 DRAM、UART0、RTC、PLIC、QEMU test/exit 和两个 VirtIO MMIO 区域，不能访问 trusted DRAM 和 UART2。
- trusted domain 只可访问 trusted DRAM、UART2 和启动必需资源，不能访问普通内核 DRAM、VirtIO、UART0/1 和 RTC。
- hart 7 不加入普通内核的页分配器、锁、调度器、中断路由和网络设备。
- domain DTB 中的 fallback 全内存区域不得给任一 domain 绕过专属区域限制的 RWX 权限。

构建系统生成两个确定性 DTB：

- `opensbi-domain.dtb`：包含完整硬件和 domain 配置，仅供 OpenSBI 建立 PMP/domain。
- `kernel.dtb`：只包含 hart 0-6、普通内核内存和可访问设备，通过 `a1` 传给普通内核。

trusted 首版不解析 DTB，`a1` 为 0，所需 UART2 和 SBI 接口由固定平台配置提供。两个 DTB 都由同一份板级资源清单生成或校验，禁止继续使用已注释生成步骤留下的旧 U-Boot DTB。

### 4.3 网络架构

TCP/IP 协议状态由一个网络核心线程串行修改。用户进程可以运行在任意普通 hart 上，但 Socket 请求必须通过消息队列进入网络线程。

```text
user application
  -> socket syscall
  -> validate/copy user arguments
  -> network request queue
  -> block current process
  -> TCP/IP core thread
  -> protocol processing
  -> wake process
  -> return result
```

收包路径：

```text
Windows/WSL network
  -> TAP
  -> QEMU VirtIO-net device
  -> RX virtqueue
  -> PLIC external interrupt
  -> descriptor reclaim
  -> netif input queue
  -> wake TCP/IP core thread
  -> Ethernet/ARP/IPv4/TCP/UDP
  -> wake socket waiter
```

发包路径：

```text
TCP/IP core thread
  -> netif output
  -> VirtIO-net TX descriptors
  -> queue notify
  -> QEMU
  -> TAP
```

### 4.4 并发边界

- TCP/IP 协议内部状态只允许网络核心线程修改。
- 中断处理程序只完成确认中断、回收描述符、入队和唤醒。
- 中断处理程序不执行 TCP/IP 协议解析。
- 页分配器、进程表、运行队列、等待队列和 VirtIO queue 分别使用独立锁。
- 会阻塞或睡眠的路径不能持有自旋锁。
- hart 7 不访问普通内核的锁、队列、页分配器和网卡。

## 5. 新仓库结构

```text
quard-star-riscv64-net/
|-- .github/
|   `-- workflows/
|       |-- build.yml
|       `-- qemu-smoke.yml
|-- docs/
|   |-- quard-star-riscv64-net-design.md
|   |-- architecture.md
|   |-- build-windows-wsl2.md
|   |-- networking.md
|   |-- smp.md
|   |-- trusted-domain.md
|   `-- third-party.md
|-- kernel/
|   |-- Makefile
|   |-- linker.ld
|   |-- arch/
|   |   `-- riscv/
|   |       |-- boot/
|   |       |-- include/
|   |       |-- mm/
|   |       |-- smp/
|   |       `-- trap/
|   |-- core/
|   |   |-- process/
|   |   |-- sched/
|   |   |-- sync/
|   |   `-- timer/
|   |-- drivers/
|   |   |-- irq/
|   |   |-- serial/
|   |   `-- virtio/
|   |       |-- virtio_mmio.c
|   |       |-- virtqueue.c
|   |       |-- virtio_blk.c
|   |       `-- virtio_net.c
|   |-- fs/
|   |   `-- fatfs_port.c
|   |-- include/
|   |-- lib/
|   |-- net/
|   |   |-- core/
|   |   |-- include/
|   |   `-- port/
|   |       |-- netif_virtio.c
|   |       |-- sys_riscv.c
|   |       `-- net_config.h
|   `-- syscall/
|       |-- syscall.c
|       `-- socket.c
|-- user/
|   |-- Makefile
|   |-- include/
|   |-- lib/
|   `-- apps/
|       |-- init/
|       |-- shell/
|       |-- ping/
|       |-- echo/
|       |-- tftp/
|       |-- httpd/
|       `-- ntp/
|-- trusted/
|   |-- Makefile
|   |-- linker.ld
|   |-- port/
|   `-- app/
|-- platform/
|   `-- quard-star/
|       |-- boot/
|       |-- dts/
|       |-- include/
|       |   `-- layout.h
|       |-- qemu/
|       `-- opensbi/
|-- patches/
|   |-- qemu/
|   `-- opensbi/
|-- scripts/
|   |-- bootstrap-wsl.sh
|   |-- build.sh
|   |-- run-qemu.sh
|   |-- tap-up.sh
|   |-- tap-down.sh
|   `-- test-network.sh
|-- tests/
|   |-- host/
|   |-- kernel/
|   `-- qemu/
|-- third_party/
|   |-- qemu/
|   |-- opensbi/
|   |-- freertos/
|   |-- dtc/
|   |-- fatfs/
|   `-- nanoprintf/
|-- .gitattributes
|-- .gitignore
|-- .gitmodules
|-- LICENSE
|-- Makefile
|-- README.md
`-- THIRD_PARTY.md
```

目录不是一次性全部创建。每个阶段只创建真正使用的目录和文件。

## 6. 第三方依赖

| 依赖 | 用途 | 集成方式 | 首版 |
|---|---|---|---|
| QEMU | RISC-V 虚拟硬件和 VirtIO 设备 | submodule + quard-star 补丁 | 必须 |
| OpenSBI | SBI、HSM、domain | submodule + 平台补丁 | 必须 |
| FreeRTOS-Kernel | hart 7 trusted domain | submodule + RISC-V port | 必须 |
| dtc/libfdt | 编译和解析设备树 | submodule | 必须 |
| FatFs | VirtIO block 文件系统 | 官方 R0.15 归档 + SHA-256 + port | 必须 |
| nanoprintf | 内核和用户态格式化输出 | submodule | 必须 |
| Mbed TLS | TLS/HTTPS | submodule | 1.0 后再评估 |
| picolibc | 用户态 C 库 | submodule | 1.0 后再评估 |

禁止加入 lwIP 或 smoltcp，因为它们会替代自研 TCP/IP 栈。

M0 必须先完成依赖锁定，才能进入构建迁移：

- QEMU 基线为官方 `v8.0.2`，只应用 `patches/qemu/series` 中按顺序列出的补丁。
- OpenSBI 基线为官方 `v1.2`，quard-star 平台代码以独立目录或补丁接入。
- FreeRTOS-Kernel、dtc/libfdt 和 nanoprintf 使用正式上游 URL，并由 submodule SHA 固定；禁止使用来源不明的镜像仓库。
- FatFs 从 Elm-Chan 官方 R0.15 归档获取，首次确认后把 URL 和 SHA-256 写入 `THIRD_PARTY.md`，后续构建只接受该校验值。
- `THIRD_PARTY.md` 记录人类可读信息，`.gitmodules` 和 gitlink 记录机器可验证 URL 与 SHA。
- `make deps` 执行 `git submodule status --recursive`、补丁 `git apply --check` 和干净工作树检查，任何一项失败立即退出。

新项目自有代码默认使用 MIT License。第三方源码、派生补丁和移植文件继续遵守各自上游许可证；例如 QEMU 派生修改不能用根目录 MIT License 覆盖。具体许可证以固定提交中的上游文件为准，并记录到 `THIRD_PARTY.md`。

`THIRD_PARTY.md` 必须记录：

- 项目名称和上游 URL。
- 固定提交 SHA。
- 使用目的。
- 许可证。
- 是否修改。
- 修改位于哪个补丁或平台目录。

第三方版权和许可证不能因为清理旧信息而删除。

## 7. 现有内核必须先修复的问题

这些问题在添加 SMP 和网络前处理，否则调试结果不可信。

### 7.1 构建系统

- 顶层脚本增加严格错误处理。
- 删除“U-Boot DTB 生成已注释，但后面仍使用”的失效路径。
- 取消固定 `make -j16`，默认使用可配置并行度。
- QEMU、OpenSBI 和 FreeRTOS 构建必须有明确输入和输出目录。
- 构建失败不得继续生成看似成功的固件。
- 使用稀疏文件创建磁盘，不读取 64 MiB `/dev/urandom`。
- 正常运行默认关闭 QEMU `-d in_asm`，调试时显式开启。
- 每个构建产物可追溯到配置和依赖版本。

### 7.2 内存和页表

- 物理页分配器改成空闲页链表，删除固定 10000 项回收数组。
- 页分配和释放加 SMP 自旋锁。
- 修复 trap frame 在进程退出后的物理页泄漏。
- `fork` 必须检查地址空间复制失败并完整回滚。
- 页表映射、取消映射和权限检查返回错误，不依赖用户输入触发 `panic`。
- 用户拷贝分别校验读、写和用户权限。
- ELF loader 校验文件范围、段大小、溢出和页对齐。

### 7.3 进程和系统调用

- 为每个 hart 提供独立的 current process、idle task 和内核栈。
- 进程状态变化由进程表锁保护。
- 实现等待队列，删除阻塞路径中的忙等 `schedule()` 循环。
- 系统调用返回值使用寄存器宽度，修复 64 位时间被 `int` 截断。
- 修正微秒时间换算。
- Socket 系统调用进入统一分发表，不复制另一套 syscall 框架。

### 7.4 输出和字符串

- 使用 nanoprintf 替换固定 1000 字节且可能越界的格式化实现。
- 内核日志和用户输出使用独立缓冲区或调用者提供缓冲区。
- Shell 输入缓冲区初始化并检查长度。
- 用户态字符串必须有最大长度和终止符校验。

### 7.5 VirtIO block

- 抽取公共 VirtIO MMIO 和 virtqueue 层。
- 正确完成 VirtIO MMIO v1 队列注册，包括 queue align 和 queue PFN。
- 校验设备返回的 descriptor id。
- 删除轮询调用中断处理函数的混合模型。
- 使用等待队列等待 I/O 完成。
- 为 block 和 net 使用独立 queue 和锁。

### 7.6 板级与 domain 基线

- 将 trusted DRAM 改为单个 8 MiB OpenSBI memregion，消除旧 DTS 中未保护的 256 KiB 空洞。
- 将 CPU `mmu-type` 从旧 DTS 的 `riscv,sv48` 改为项目实际使用的 `riscv,sv39`。
- 将 RTC IRQ 修正为 13，禁止与 UART1 的 IRQ 11 冲突。
- 删除 OpenSBI 平台操作表中重复的 `ipi_init` 和 `ipi_exit` 初始化项。
- 删除失效的 U-Boot DTB 路径，分别生成并校验 `opensbi-domain.dtb` 和 `kernel.dtb`。
- QEMU、DTS 和内核共用同一资源定义测试，发现地址、大小或 IRQ 不一致时构建失败。

### 7.7 协议栈基线

- 只迁移 `tiny-tcpip-stack` 固定提交中的 `code/pc/src/net` 实现。
- 修复 `exmsg_func_exec()` 在消息块耗尽后未检查空指针的问题，池耗尽返回 `NET_ERR_MEM`。
- 审计从 PC 到 RV64 的 `sizeof`、指针宽度、整数截断、结构体对齐、非对齐访问、大小端和校验和实现。
- `sys_time_curr()` 和 `sys_time_goes()` 使用单调毫秒时钟，正确处理计数回绕，不使用墙上时间驱动 TCP 重传。
- host 测试先覆盖协议解析、定时器、内存池和 TCP 状态机，再连接内核平台层。

## 8. SMP 实施设计

### 8.1 启动

1. OpenSBI 根据 DTS 建立 trusted 和 untrusted domain。
2. hart 0 作为普通内核 boot hart。
3. hart 0 初始化早期页表、内存、PLIC、调度器和 per-CPU 数据。
4. hart 0 通过 SBI HSM 启动 hart 1。
5. hart 1 使用独立启动栈，切换到内核页表。
6. hart 1 标记 online，然后进入自己的 idle task。
7. 双 hart 稳定后用同一路径启动 hart 2-6。

禁止使用固定延时等待主 hart 完成初始化。所有跨 hart 状态使用原子变量和正确内存序。

### 8.2 每 hart 数据

每个普通 hart 至少保存：

- hart id 和逻辑 CPU id。
- online/started 状态。
- 当前进程。
- idle task。
- 调度器嵌套状态。
- 中断嵌套深度。
- 独立内核启动栈。
- 本地 timer 状态。

### 8.3 同步原语

按顺序实现：

1. 自旋锁。
2. irq-save 自旋锁。
3. 等待队列。
4. 计数信号量。
5. 睡眠互斥锁。
6. 跨核唤醒/IPI。

调试构建记录锁持有者、调用位置和中断状态。首版不实现复杂锁依赖图。

### 8.4 调度器

- 首版使用一个全局运行队列和一个调度锁。
- 每个 hart 有独立 idle task。
- timer 中断只设置调度条件，不在不安全位置直接切换。
- 进程可以在 hart 0 和 hart 1 间迁移。
- 后续扩到 7 个普通 hart 时不改变进程和调度接口。

### 8.5 IPI、TLB 和地址空间

- SBI HSM 用于启动和查询指定 hart；hart 列表只来自 `kernel.dtb` 的 CPU 节点。
- SBI IPI 用于远程调度唤醒，SBI RFENCE 用于远程 `sfence.vma`。
- 首版所有地址空间使用 ASID 0；切换地址空间时执行本地 `sfence.vma`，不提前实现 ASID 分配器。
- 取消映射、降低权限或回收可能仍被其他 hart 使用的物理页前，必须完成目标 hart 的 TLB shootdown。
- shootdown 请求包含地址空间、虚拟地址范围和完成计数；发起者等待所有目标 hart 确认后才能重用物理页。
- hart 启动状态使用 release/acquire 发布，禁止以 `volatile` 或固定延时替代原子同步。

### 8.6 锁和睡眠规则

- 自旋锁同时禁止本 hart 抢占；irq-save 自旋锁还保存并关闭本 hart 中断状态。
- 允许的嵌套顺序为：进程表锁 -> 运行队列锁，VirtIO 设备状态锁 -> 对应 virtqueue 锁。其他跨子系统锁不嵌套。
- 页分配器锁、等待队列锁和网络请求队列锁不允许在获取其他睡眠锁后继续持有。
- 任何 `schedule()`、信号量等待、I/O 等待或网络请求等待前都必须释放自旋锁。
- 调试构建在违反锁顺序、递归加锁、持锁睡眠或错误中断状态时立即报告调用点。

## 9. VirtIO-net 与 TAP

### 9.1 QEMU 设备

quard-star machine 固定创建两个 VirtIO MMIO transport。bus 0 连接 block，bus 1 连接 net，地址和 IRQ 使用 4.2 节的资源表。QEMU 启动参数为：

```text
-global virtio-mmio.force-legacy=true
-drive file=out/images/disk.img,if=none,format=raw,id=vd0
-device virtio-blk-device,drive=vd0,bus=virtio-mmio-bus.0
-netdev tap,id=net0,ifname=tap0,script=no,downscript=no
-device virtio-net-device,netdev=net0,mac=52:54:00:12:34:56,bus=virtio-mmio-bus.1
```

首版明确使用 legacy VirtIO MMIO v1，与 queue PFN 和 queue align 接口保持一致。`info qtree`、DTS 解析测试和启动日志必须验证两个 bus 的设备、MMIO 地址和 IRQ；以后迁移 modern MMIO v2 时作为独立里程碑，不在同一驱动中混用两套注册方式。

### 9.2 驱动范围

首版驱动支持：

- 设备探测和状态协商。
- MAC 地址获取。
- 单 RX queue 和单 TX queue。
- RX 缓冲区预投递。
- TX descriptor 提交。
- 中断确认和 descriptor 回收。
- 链路统计和错误计数。
- 设备 reset 和重新初始化。

首版不做：

- checksum offload。
- segmentation offload。
- control virtqueue。
- multiqueue。
- mergeable receive buffers。

不支持的 feature 必须在协商阶段清除。

设备初始化必须依次完成 reset、ACKNOWLEDGE、DRIVER、feature 读取与清除、FEATURES_OK 回读、queue 注册和 DRIVER_OK。任何状态回读失败都停止设备，不继续提交 descriptor。

### 9.3 RX/TX 所有权

首版采用复制路径，不做零拷贝：

- RX 缓冲区预投递后归设备所有；used ring 返回后归驱动所有。
- 驱动校验 `virtio_net_hdr` 和帧长度，将 Ethernet frame 复制到协议栈 `pktbuf`，随后立即重新投递 RX 缓冲区。
- `pktbuf` 入 netif 队列后归网络核心线程所有，解析失败或处理完成时由协议栈释放。
- TX 时驱动把 `pktbuf` 内容复制到固定 TX bounce buffer，提交成功后协议栈即可释放原 `pktbuf`。
- TX used ring 返回后，驱动回收 descriptor 和 bounce buffer，并唤醒等待发送空间的线程。
- reset 时先禁止新提交，再失败所有等待者，回收全部在途 descriptor 和 buffer，最后重建 queue。

中断处理程序不得调用可能睡眠的 `mblock_alloc(..., -1)`、`fixq_send(..., -1)` 或协议解析函数。ISR 只把完成项写入固定容量、无阻塞的驱动完成队列并唤醒驱动/网络线程；队列满时增加丢包或错误计数，不能阻塞。

### 9.4 TAP 地址规划

首版测试网段：

```text
WSL/TAP: 192.168.100.1/24
guest:   192.168.100.2/24
mask:    255.255.255.0
gateway: 192.168.100.1
```

先验证 WSL2 Linux 环境到 guest 的二层和三层通信。Windows 主机直接访问 guest 受 WSL2 网络模式影响，不作为第一阶段阻塞条件。

## 10. TCP/IP 协议栈迁移

### 10.1 直接保留

在许可证和来源说明完整的前提下，迁移并整理：

- Ethernet。
- ARP。
- IPv4。
- ICMPv4。
- RAW socket。
- UDP。
- TCP 状态机、输入、输出和重传。
- DNS。
- Loopback。
- pktbuf、mblock、fixq、nlist 和 timer。
- Socket 和 net API。

### 10.2 删除的平台代码

新仓库不导入：

- Npcap/pcap 平台驱动。
- Windows HANDLE、线程和信号量实现。
- STM32、LAN8720 和 Keil 工程。
- x86 boot、GDT/IDT、分页和 RTL8139 驱动。
- 旧 Visual Studio 工程文件。
- 旧网页演示资产、编译输出、反汇编文件和统计工具输出。

### 10.3 RISC-V 平台适配

`kernel/net/port` 实现协议栈需要的最小接口：

- 当前时间和时间差。
- 睡眠。
- 线程创建、退出和当前线程。
- 信号量创建、等待、通知和销毁。
- 互斥锁创建、加锁、解锁和销毁。
- 中断保护。
- 内存分配接口。
- 日志接口。
- VirtIO-net netif 输入输出。

接口语义保持与现有协议栈一致，不为 RISC-V 复制一套协议实现。

现有协议栈的 `exmsg` 消息队列就是唯一网络请求队列。Socket syscall 适配层调用现有 `x_*`/socket 核心接口，并通过 `exmsg_func_exec()` 串行进入网络核心线程；禁止再增加第二套 RPC 队列。RISC-V 平台层必须保持现有信号量约定：等待时间 0 表示永久等待，正数表示毫秒超时。

VirtIO RX 完成先进入驱动完成队列，再由线程调用非中断版本的 netif 输入接口。不得从 PLIC ISR 直接调用当前可能阻塞的 `exmsg_netif_in()`。

## 11. Socket 和用户态

### 11.1 内核接口

首版提供：

- `socket`
- `close`
- `bind`
- `connect`
- `listen`
- `accept`
- `send`
- `recv`
- `sendto`
- `recvfrom`
- 基本地址和错误码结构

暂不追求完整 POSIX flags 和全部 socket options。

### 11.2 ABI 和对象生命周期

- Socket 作为内核文件对象进入每进程 fd 表，用户态只看到非负整数 fd。
- syscall 参数使用固定宽度结构；指针和长度按 RV64 寄存器宽度传递，返回值使用有符号 `ssize_t`/`intptr_t` 宽度。
- 内核先复制并校验地址结构、长度和输入数据，再向网络线程提交请求；网络线程不直接解引用用户地址。
- `fork` 复制 fd 引用并增加 socket 引用计数；首版没有 `FD_CLOEXEC`，因此 `exec` 保留 fd；`exit` 关闭该进程持有的全部引用。
- `close` 与阻塞请求并发时先标记关闭并取消该进程的等待；最后一个引用释放后才销毁协议栈 socket。
- `send` 和 `recv` 允许部分完成；流式 `recv` 返回 0 表示对端有序关闭。
- 首版只提供阻塞模式。超时、连接复位、不可达、内存不足和无效参数映射为稳定的负 errno；协议栈内部错误码不直接暴露给用户态。
- fd、socket、请求、pktbuf 或 descriptor 池耗尽时返回错误，不调用 `panic`。

### 11.3 阻塞模型

- 阻塞 `connect`、`accept`、`recv` 使用等待队列。
- 数据、连接状态、错误或超时发生时唤醒进程。
- 进程退出时关闭所有 Socket 并取消等待。
- 网络线程不能持有协议内部锁等待用户进程。
- 每个阻塞请求拥有独立完成状态和等待节点；超时、信号式取消、进程退出和设备 reset 只能完成请求一次。
- 唤醒方先写入结果再以 release 语义发布完成状态；等待方以 acquire 语义读取结果，防止跨 hart 丢失完成信息。

### 11.4 示例应用

按顺序移植：

1. Ping。
2. UDP Echo client/server。
3. TCP Echo client/server。
4. DNS client。
5. TFTP client/server。
6. HTTP server/client。
7. NTP client。

## 12. FatFs 与文件访问

- FatFs 通过磁盘接口连接 VirtIO block。
- 首版只挂载一个测试卷。
- TFTP 和 HTTP 使用统一文件接口，不直接操作 block buffer。
- 测试镜像由脚本确定性生成，不提交随机磁盘镜像。
- 文件系统损坏返回错误，不导致内核崩溃。

## 13. trusted FreeRTOS domain

### 13.1 保留范围

- hart 7 由 OpenSBI trusted domain 启动。
- 使用官方 FreeRTOS-Kernel submodule。
- FreeRTOS 作为 OpenSBI 的 S-mode next stage 运行，保留最小 RISC-V S-mode port 和 UART2 演示。
- trusted domain 有独立链接地址、内存区域和串口/MMIO 权限。
- trusted 链接范围固定为 `0xBF800000-0xBFFFFFFF`，代码、数据、heap 和栈不能越界。
- timer 通过 SBI TIME 编程。首版 trusted demo 不使用软件 IPI 和 PLIC 外部中断，UART2 使用轮询输出，避免共享 PLIC 破坏 domain 边界。
- trusted 编译固定使用 RV64 integer ABI；除非端口实现并测试完整浮点上下文保存，否则不允许编译器生成浮点指令。
- `FreeRTOSConfig.h`、S-mode port 和链接脚本属于主仓库平台代码，FreeRTOS-Kernel submodule 保持干净。

### 13.2 首版隔离

- 不共享 VirtIO-net。
- 不共享普通内核页分配器。
- 不共享锁和调度器。
- 不实现跨 domain RPC。
- 不允许 trusted domain 访问普通内核 DRAM，即使它在逻辑上被称为 trusted。

主项目网络稳定后，可以单独设计“trusted network service”，但它不属于 1.0 必须项。

## 14. 错误处理

### 14.1 可以返回的错误

以下情况返回错误码，不 `panic`：

- 非法用户地址。
- 无效系统调用参数。
- Socket 状态错误。
- 端口冲突。
- 包、Socket、descriptor 或内存池耗尽。
- ARP、TCP、DNS 和 I/O 超时。
- 网络包格式或校验和错误。
- 文件不存在或文件系统错误。

### 14.2 允许 panic 的情况

只在内核不变量已经被破坏时 panic，例如：

- 页表结构损坏。
- 双重释放且无法安全恢复。
- VirtIO ring 索引证明发生内存破坏。
- 锁状态不可能成立。
- 启动阶段必需硬件不存在。

### 14.3 设备故障

VirtIO 设备故障时：

1. 停止提交新请求。
2. 唤醒并失败所有等待请求。
3. reset 设备。
4. 重建 queue。
5. 成功后恢复，失败则保持设备离线并报告。

## 15. 实施阶段

### M0：新仓库和来源清理

- 创建全新空仓库。
- 配置新账号作者名称和已验证邮箱。
- 添加 README、LICENSE、`.gitattributes` 和 `.gitignore`。
- 添加第三方 submodule 和 `THIRD_PARTY.md`。
- 导入设计文档。
- 固定两个自有源仓库提交和唯一迁移目录，记录迁移矩阵。
- README 增加 rCore 教学思路致谢和自有/第三方代码边界说明。
- 不导入任何旧 `.git`。

验收：提交历史只包含新账号。

### M1：单核基线和构建修复

- 提取自有 QEMU/OpenSBI 修改。
- 建立可重复构建。
- 修复构建脚本、时间、用户拷贝、输出和内存泄漏。
- 修复 domain 内存、Sv39、RTC IRQ、OpenSBI IPI 初始化和双 DTB 基线。
- hart 7 以 S-mode 启动 FreeRTOS，并只使用 trusted DRAM 和 UART2。
- 单核启动、进程和 VirtIO block 回归通过。

验收：全新 WSL2 环境可一键构建并启动单核系统。

### M2：双 hart SMP

- per-CPU 数据和启动栈。
- SBI HSM secondary 启动。
- 自旋锁、等待队列、信号量、睡眠锁。
- SMP 安全页分配、进程表、调度和 timer。
- 远程调度 IPI、SBI RFENCE 和 TLB shootdown。
- 双 hart 压力测试。

验收：hart 0/1 连续运行调度和内存压力测试，无死锁和泄漏。

### M3：公共 VirtIO 层

- 公共 VirtIO MMIO 和 virtqueue。
- 修复 VirtIO block。
- 中断驱动 I/O 和等待队列。

验收：FatFs 测试卷连续读写和校验通过。

### M4：VirtIO-net 和 TAP

- VirtIO-net RX/TX。
- QEMU 和 DTS 增加第二个 VirtIO MMIO transport、地址和 IRQ。
- TAP 脚本。
- 复制式 RX/TX ownership、中断完成队列、统计和 reset。

验收：`tcpdump` 看到双向 Ethernet frame，驱动压力测试无 descriptor 泄漏。

### M5：协议栈平台层和 Ping

- 迁移通用协议核心。
- 实现 RISC-V 平台层和 VirtIO netif。
- 完成 ARP、IPv4、ICMP。

验收：WSL2 与 guest 双向 Ping，ARP 表和包统计正确。

### M6：Socket、UDP 和 TCP

- Socket syscall 和用户库。
- fd 生命周期、阻塞等待、超时、取消和进程退出清理。
- UDP/TCP Echo。

验收：并发 Echo、断连、重传和超时测试通过。

### M7：DNS、TFTP、HTTP 和 NTP

- DNS client。
- FatFs 文件接口。
- TFTP、HTTP 和 NTP 示例。
- 在独立网络命名空间启动本地 DNS、TFTP、HTTP、NTP 和 Echo 测试服务，不依赖公网。

验收：域名解析、文件传输、HTTP 下载和时间查询均可复现。

### M8：扩到 7 个普通 hart

- 启用 hart 2-6。
- 调度、内存、网络和文件系统并发压力测试。
- hart 7 FreeRTOS 同时运行。

验收：7 个普通 hart 和 trusted hart 连续运行，无锁死、页泄漏和网络中断丢失。

### M9：CI、文档和 1.0

- GitHub Actions 构建。
- QEMU headless smoke test。
- SMP 和网络端到端测试。
- 架构、构建、调试和限制文档。
- 1.0 release。

## 16. 测试策略

### 16.1 主机单元测试

协议解析、校验和、队列、内存池和 TCP 状态机可在 WSL2 主机上编译测试。主机测试适配器只存在于 `tests/host`，不重新引入旧 PC 产品工程。

- 自有 C 代码使用 `-Wall -Wextra -Werror`；host 测试同时运行 AddressSanitizer 和 UndefinedBehaviorSanitizer。
- 每种 Ethernet、ARP、IPv4、ICMP、UDP 和 TCP 首部都覆盖截断、超长、错误长度、错误校验和和不支持选项。
- 固定测试种子和输入 corpus，失败时输出 seed 和最小输入，保证可复现。
- 协议栈池耗尽测试必须返回错误，不能空指针解引用、越界或死锁。

### 16.2 内核自测

- 原子操作和锁竞争。
- 页分配/释放和双重释放检测。
- 多 hart 调度和迁移。
- 信号量超时与唤醒。
- 用户地址跨页复制和权限检查。
- `fork/exec/wait/exit` 压力测试。
- VirtIO descriptor 分配和回收。

双 hart CI 压力测试至少运行 120 秒，开发机发布前测试至少运行 10 分钟，并完成不少于 100000 次页分配/释放和 10000 次跨 hart 调度迁移。结束时空闲页数、可用 descriptor 数和等待请求数必须回到测试前基线。

### 16.3 QEMU 端到端测试

- 串口启动标记。
- hart online 数量。
- FreeRTOS trusted domain 启动标记。
- TAP 链路和 MAC 地址。
- ARP 和 Ping。
- UDP/TCP Echo。
- DNS、TFTP、HTTP、NTP。
- 设备 reset 和网络超时。

端到端测试使用稳定串口标记：`QS:BOOT_OK`、`QS:HART_ONLINE:<id>`、`QS:TRUSTED_READY`、`QS:TEST_PASS:<name>` 和 `QS:TEST_FAIL:<name>:<code>`。测试结束通过 `0x00100000` QEMU test/exit 设备退出，脚本同时检查进程退出码和串口标记，不能只靠日志超时猜测成功。

网络验收至少包括 1000 个双向 ICMP/UDP 数据包、8 个并发 TCP Echo 连接、100 次 TCP 重连、1 个不小于 1 MiB 的 TFTP 文件 SHA-256 校验，以及每种设备 reset 场景 10 次。结束时所有 VirtIO descriptor 和 bounce buffer 必须回到空闲池。

### 16.4 外部工具

- `ping`
- `arp` 或 `ip neigh`
- `tcpdump`
- `nc`
- `curl`
- `tftp`
- `sha256sum`

## 17. Windows 开发环境

### 17.1 推荐方案

使用 Windows 11 作为桌面系统，所有编译、QEMU、TAP 和测试在 WSL2 Ubuntu 中运行。

不推荐首版使用纯 Windows 原生工具链，原因：

- TAP、网络命名空间和 Linux 网络工具在 WSL2 中更统一。
- 构建脚本、Make、交叉编译器和 CI 与 Linux 保持一致。
- 避免同时维护 PowerShell、Batch 和 Shell 三套流程。
- QEMU headless 调试更容易复现。

### 17.2 安装 WSL2

以管理员身份打开 PowerShell：

```powershell
wsl --install -d Ubuntu
wsl --update
```

重启后进入 Ubuntu，创建 Linux 用户。

可选：Windows 11 支持时，在 `%UserProfile%\.wslconfig` 中启用 mirrored networking：

```ini
[wsl2]
networkingMode=mirrored
dnsTunneling=true
firewall=true
```

修改后执行：

```powershell
wsl --shutdown
```

mirrored networking 不是 guest 网络第一阶段的硬依赖。第一阶段只要求 WSL2 与 QEMU guest 互通。

### 17.3 WSL2 基础依赖

在 Ubuntu 中执行：

```bash
sudo apt update
sudo apt install -y \
  build-essential git make ninja-build cmake pkg-config \
  gcc-riscv64-unknown-elf binutils-riscv64-unknown-elf \
  gdb-multiarch device-tree-compiler \
  python3 python3-venv \
  libglib2.0-dev libpixman-1-dev zlib1g-dev libslirp-dev \
  flex bison bc \
  iproute2 bridge-utils tcpdump netcat-openbsd curl tftp-hpa \
  dnsmasq-base tftpd-hpa chrony socat
```

实施时由 `scripts/bootstrap-wsl.sh` 检查命令和版本。脚本默认只报告缺失依赖，不擅自修改系统配置。

### 17.4 仓库位置

仓库放在 WSL2 Linux 文件系统，例如：

```bash
mkdir -p ~/src
cd ~/src
git clone --recursive https://github.com/Quchaosheng/quard-star-riscv64-net.git
```

不要把构建目录放在 `/mnt/c`，否则大量小文件操作和 QEMU 源码构建会明显变慢。

Windows 可以通过资源管理器访问：

```text
\\wsl.localhost\Ubuntu\home
```

进入上面的 `home` 后，再打开创建 WSL2 时使用的 Linux 用户目录和 `src\quard-star-riscv64-net`。

推荐 VS Code 安装 Remote - WSL 扩展，并从 WSL 目录执行：

```bash
code .
```

### 17.5 Git 新账号配置

只在新仓库中配置，不覆盖其他项目的全局身份：

```bash
git config user.name "Quchaosheng"
read -rp "GitHub verified email: " NEW_GIT_EMAIL
git config user.email "$NEW_GIT_EMAIL"
unset NEW_GIT_EMAIL
git config --get user.name
git config --get user.email
```

输入的邮箱必须先出现在新账号 GitHub `Settings -> Emails` 中并处于 verified 状态；也可以使用 GitHub 为新账号提供的 noreply 邮箱。

推送前检查：

```bash
git log --format='%h %an <%ae> %s'
git shortlog -sne HEAD
git remote -v
```

不得出现：

- `yanglianoo`
- `330070781@qq.com`，除非这个邮箱已经验证并绑定到新账号
- 旧仓库 remote URL

### 17.6 换行规则

根目录添加 `.gitattributes`：

```gitattributes
* text=auto
*.c text eol=lf
*.h text eol=lf
*.S text eol=lf
*.s text eol=lf
*.ld text eol=lf
*.lds text eol=lf
*.dts text eol=lf
*.sh text eol=lf
Makefile text eol=lf
*.md text eol=lf
*.ps1 text eol=crlf
*.bat text eol=crlf
```

这样 Windows 编辑器不会把 Shell 和 Makefile 攇成 CRLF。

## 18. WSL2 TAP 配置

### 18.1 检查 TUN/TAP

```bash
test -c /dev/net/tun && echo "TUN/TAP available"
```

如果 `/dev/net/tun` 不存在，先更新 WSL，再重新启动。不要在不知道内核能力的情况下手工伪造设备节点。

### 18.2 创建 TAP

```bash
sudo ip tuntap add dev tap0 mode tap user "$USER"
sudo ip addr add 192.168.100.1/24 dev tap0
sudo ip link set tap0 up
ip addr show tap0
```

删除 TAP：

```bash
sudo ip link set tap0 down
sudo ip tuntap del dev tap0 mode tap
```

这些命令最终由 `scripts/tap-up.sh` 和 `scripts/tap-down.sh` 管理，脚本必须可重复执行。

### 18.3 网络验证

QEMU 启动后：

```bash
ping 192.168.100.2
sudo tcpdump -ni tap0 -e -vv
ip neigh show dev tap0
```

Windows 主机直接访问 guest 可能受 WSL2 NAT、防火墙和 mirrored networking 影响。先确保 WSL2 到 guest 稳定，再单独处理 Windows 到 guest 的暴露方式。

### 18.4 确定性本地服务

自动化测试默认在独立 Linux network namespace 中创建 TAP，并在 `192.168.100.1` 启动：

- `dnsmasq`：解析固定测试域名 `quard.test`。
- `in.tftpd`：提供固定输入文件并接收上传文件。
- `python3 -m http.server`：提供固定 HTTP 内容。
- `chronyd -x`：只提供本地 NTP 响应，不调整 WSL 系统时间。
- `socat`：提供 UDP 和 TCP Echo 对端。

guest 固定使用 `192.168.100.1` 作为网关和 DNS 服务器。1.0 的 CI 和完成定义不要求 IP forwarding、NAT 或公网可用；公网访问只作为手工演示，不能替代本地验收。

## 19. 构建和运行接口

根目录只暴露少量稳定命令：

```bash
make deps       # 初始化和检查 submodule
make qemu       # 构建固定版本自定义 QEMU
make firmware   # 构建 boot/OpenSBI/trusted/kernel/user
make image      # 生成固件和文件系统镜像
make run        # headless 启动
make debug      # GDB stub 启动
make test       # 主机测试和 QEMU smoke test
make clean      # 删除项目生成物
```

顶层 Makefile 只编排，不复制每个子项目的内部构建规则。

构建产物统一位于：

```text
out/
|-- qemu/
|-- firmware/
|-- kernel/
|-- trusted/
|-- user/
|-- images/
`-- logs/
```

`out/` 不提交到 Git。

## 20. CI 设计

### 20.1 build workflow

- 检出 submodule。
- 安装固定依赖。
- 构建主机测试。
- 构建 QEMU、OpenSBI、kernel、trusted 和 user。
- 检查警告和产物。

### 20.2 QEMU smoke workflow

- 创建网络命名空间和 TAP。
- headless 启动 QEMU。
- 等待串口 ready 标记。
- 验证双 hart online。
- 验证 trusted domain ready。
- 执行 Ping 和 Echo。
- 保存串口、QEMU 和 tcpdump 日志。
- 超时必须终止 QEMU，不允许 CI 永久挂起。
- 无论成功或失败都删除 network namespace、TAP 和后台测试服务。

网络命名空间和 TAP 需要 Linux runner 权限；如果 GitHub 托管 runner 限制某一步，则保留无 TAP smoke test，并在自托管 runner 执行完整网络测试。

## 21. 后续优化清单

这些优化必须有测量数据后再做：

- 全局运行队列改为每核运行队列。
- 任务亲和性和负载均衡。
- VirtIO-net multiqueue。
- RX/TX 零拷贝。
- checksum 和 segmentation offload。
- 网络线程 CPU affinity。
- 更细粒度 Socket 锁。
- 大页或页表优化。
- slab allocator。
- IPv6。
- Mbed TLS 和 HTTPS。
- trusted domain 网络代理。

每项优化必须提供优化前后基准和回归测试，不以代码复杂度作为成果。

## 22. 新 Git 历史和旧信息清理

### 22.1 必须执行

1. 在新账号下创建空仓库。
2. 不让 GitHub 自动创建 README、`.gitignore` 或 License。
3. 本地从空目录 `git init` 或克隆空仓库。
4. 只复制选定源码文件，不复制任何旧 `.git`。
5. 不使用保留旧作者的 merge、subtree 或历史导入。
6. 依赖通过 submodule 指向正式上游。
7. 使用新账号已验证邮箱创建全部提交。

### 22.2 删除的旧内容

- 旧 Git 作者和 committer 元数据。
- 旧 remote URL。
- x86、Windows、STM32 平台工程。
- RTL8139、Npcap 和 LAN8720 平台驱动。
- Visual Studio 和 Keil 工程文件。
- 编译产物、反汇编输出、随机磁盘镜像和大体积网页演示资源。
- 复制进仓库的完整 QEMU、OpenSBI、U-Boot 和 FreeRTOS 源码。

### 22.3 不能删除的内容

- 第三方许可证。
- 第三方版权声明。
- 从第三方项目派生的文件头。
- 自有源码依法需要保留的来源和许可证信息。

### 22.4 发布前检查

```bash
git shortlog -sne HEAD
git log --all --format='%an <%ae>' | sort -u
git remote -v
git status --short
git submodule status --recursive
```

同时搜索旧账号和旧邮箱。任何命中都要判断是 Git 元数据、文档说明还是不应存在的旧信息。

## 23. 风险与应对

| 风险 | 应对 |
|---|---|
| 当前内核不是 SMP safe | 网络移植前先完成 M2 SMP 基础 |
| 自定义 QEMU 修改难以从完整源码分离 | 与固定上游 QEMU 8.0.2 做最小补丁提取 |
| VirtIO block 当前初始化不完整 | M3 先建立公共 virtqueue 并修复 block |
| WSL2 TAP 与 Windows 直连差异 | 首先验收 WSL2 到 guest，再处理 Windows 暴露 |
| 网络栈依赖线程和信号量 | 复用现有接口语义，在内核实现最小原语 |
| 协议栈存在多份不同副本 | 只允许迁移固定提交的 `code/pc/src/net` |
| block 和 net 需要两个 MMIO transport | QEMU、DTS 和 `layout.h` 同时增加 VIRTIO1，并做一致性测试 |
| domain 配置错误破坏双向隔离 | 使用单个 8 MiB trusted region，并在 QEMU 测试中验证越权访问失败 |
| 7 核扩展暴露锁竞争 | 双核先正确，7 核压力测试后再优化 |
| trusted domain 增加调试复杂度 | 保持数据完全隔离，不让它阻塞网络主线 |
| 第三方许可证混乱 | submodule + THIRD_PARTY.md + 保留原许可证 |
| 范围过大 | 每个里程碑都必须独立可运行和验收 |

## 24. 完成定义

项目 1.0 只有同时满足以下条件才算完成：

- 新 Git 历史只有新账号作者。
- Windows 11 + WSL2 Ubuntu 24.04或26.04 LTS 文档可从空环境复现。
- QEMU、OpenSBI、FreeRTOS、dtc/libfdt 和 nanoprintf 均为固定 submodule，FatFs 为固定 SHA-256 的官方归档。
- hart 0-6 运行 SMP 内核，hart 7 运行 trusted FreeRTOS。
- VirtIO block、FatFs 和 VirtIO net 稳定运行。
- TAP 上可以观察和验证 ARP、ICMP、UDP 和 TCP。
- 用户程序可以使用 Socket API。
- Ping、Echo、DNS、TFTP、HTTP 和 NTP 示例通过。
- 主机测试、内核测试和 QEMU 端到端测试通过。
- README 清楚区分自有代码、移植代码和第三方依赖。
- README 包含 rCore 教学思路致谢，但不将其声明为代码依赖。
- QEMU 板级资源、两个 DTB、OpenSBI domain 和内核驱动通过自动一致性检查。
- CI 网络测试只依赖本地确定性服务即可通过，公网不可用不影响 1.0 验收。
- 已知限制和未完成优化有明确记录。

## 25. 下一步

开始实现前按顺序完成：

1. 确认本地 remote 指向 `https://github.com/Quchaosheng/quard-star-riscv64-net.git`。
2. 在本仓库配置新账号已验证提交邮箱，不修改全局 Git 身份。
3. 在 WSL2 Ubuntu 24.04或26.04 LTS 中克隆空仓库并运行环境依赖检查。
4. 按 3.4 节迁移矩阵和第 6 节依赖规则完成 M0。
5. 为 M1-M9 创建逐阶段实施计划和可运行验收命令。
6. 从 M1 开始顺序执行，不跨阶段堆积未验证代码。

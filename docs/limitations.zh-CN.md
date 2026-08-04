# 当前限制

[English](limitations.md) | **简体中文**

- 网络栈仅实现 IPv4，尚未实现 IPv6、DHCP、TLS、HTTPS 和网络卸载。
- Guest 网络使用固定的 `192.168.100.0/24` 测试网段，公网访问不属于验收测试。
- TFTP 客户端仅实现已测试的读取路径和 `windowsize=4`，不是通用 TFTP 客户端或服务端。
- TCP 是经过测试的子集，不是完整 RFC 实现。M8 只通过 HTTP 测试客户端路径；专门的 M6C1/M6C2 冒烟和压力阶段才覆盖已测试的握手、重传、关闭、监听/接收、echo 和压力路径。载荷发送采用停等式，单段最多 512 B，固定 RTO 为 500 ms；未实现 TCP 选项、拥塞控制、RTT 估计和 SACK。
- 文件系统调用层是带 4 个代次校验句柄的小型 FatFs 测试接口，不是 POSIX VFS。
- OpenSBI 的 domain 配置分配 PMP 权限，将 hart 7 的 8 MiB 可信内存和 UART2 与 hart 0-6 隔离，同时将普通内存与 hart 7 隔离。双向读、写和取指 fault 只构成 QEMU 模型证据，不代表物理硬件、DMA 隔离或侧信道安全。
- 可信固件调度标记通过 UART2 采集，不涉及硬件紧急停机或真实物理设备声明。
- QEMU/TAP 验收运行于 Linux 或 WSL2；原生 Windows 网络不属于验收环境。
- 定时 M8 GitHub Actions 任务依赖托管 runner 的 TAP 权限，并从源码构建打过补丁的 QEMU 机器。

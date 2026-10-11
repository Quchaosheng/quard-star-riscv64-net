# 构建与调试

[English](build-debug.md) | **简体中文**

## 环境

请直接使用 Ubuntu 24.04/26.04，或通过 WSL2 使用。构建前，`make check-env` 会检查
受支持的 Ubuntu 版本、必需命令、`glib-2.0` 与 `pixman-1` 的 pkg-config 条目，
以及可链接的 `libfdt` 开发文件。它不会安装软件包或初始化子模块。

```sh
make check-env
make deps
```

`make deps` 会初始化锁定版本的子模块，并下载 `third_party/fatfs.lock` 中记录
的 FatFs 压缩包。

## 测试

无需 QEMU 或 TAP 即可运行主机测试：

```sh
make test-host
```

构建并运行与当前源码绑定的完整系统验收：

```sh
make m8-build
sudo -E make m8-smoke
```

CI 在构建后直接使用 `sudo -E make m8-smoke`。`make run` 要求先由
`make m8-build` 生成缓存产物，不重新构建，并通过 `sudo` 委托 `m8-smoke`；
但它会先用 `out/m8/manifest.json` 校验当前工作树。源码变化、补丁或依赖锁文件
变化、产物被替换、提交号不同或清单缺失，都会以具名原因和 `make m8-build`
指令终止运行。如需有意运行更旧的构建，请使用
`QS_ALLOW_STALE_ARTIFACT=1 make run`；此时运行及其证据会带上
`QS:M8_BUILD_IDENTITY`，标明实际执行的构建标识。

冒烟测试会创建 `tap0`、分配 `192.168.100.1/24`、启动本地原始数据包对端，
并在退出时移除 TAP 设备。测试不使用公网 DNS 或互联网服务。

## 日志

M8 输出保存在 `out/m8`：

- `kernel.log`：OpenSBI 和普通内核 UART 输出。
- `trusted.log`：hart 7 FreeRTOS UART2 输出。
- `qemu.err`：QEMU 诊断信息。
- `m5-peer.stats`：TAP 数据交换计数器。

成功运行的 `kernel.log` 必须包含 `QS:TEST_PASS:m8-smoke` 和
`QS:PMP_UNTRUSTED_DENY_OK`。`trusted.log` 必须包含 `QS:TRUSTED_READY`、
`QS:TRUSTED_SCHED_OK` 与 `QS:PMP_TRUSTED_DENY_OK`。任何
`QS:TEST_FAIL` 标记都优先于之后的输出。

## 内核调试

内核和可信固件会保留带符号的 ELF 文件：

```sh
gdb-multiarch kernel/os.elf
gdb-multiarch trusted/build/trusted_fw.elf
```

遇到协议故障时，应先检查第一个缺失的稳定标记和 `m5-peer.stats`，再考虑调整
超时。调查 FatFs 分配失败时，请从新生成的磁盘镜像重新构建 M7E 或 M8。

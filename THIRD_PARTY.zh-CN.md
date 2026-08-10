# 第三方软件

[English](THIRD_PARTY.md) | **简体中文**

第三方源码保留其上游许可证。Git 依赖由仓库 gitlink 固定版本；FatFs 由公开
镜像的固定提交归档和 SHA-256 固定版本。

| 组件 | 上游 | 版本 | 固定版本 | 许可证 | 用途 | 本地修改 |
| --- | --- | --- | --- | --- | --- | --- |
| QEMU | [qemu-project/qemu](https://gitlab.com/qemu-project/qemu) | `v8.0.2` | `f7f686b61cf7ee142c9264d2e04ac2c6a96d37f8` | GPL-2.0-or-later 及上游说明的其他许可证 | quard-star 机器与虚拟设备 | 子模块保持干净；项目修改位于 `patches/qemu/` |
| OpenSBI | [riscv-software-src/opensbi](https://github.com/riscv-software-src/opensbi) | `v1.2` | `6b5188ca14e59ce7bf71afe4e7d3d557c3d31bf8` | BSD-2-Clause | SBI、HSM 和 domain | 子模块保持干净；项目修改位于 `patches/opensbi/` |
| FreeRTOS-Kernel | [FreeRTOS/FreeRTOS-Kernel](https://github.com/FreeRTOS/FreeRTOS-Kernel) | `V10.5.1` | `def7d2df2b0506d3d249334974f51e427c17a41c` | MIT | hart 7 可信 domain | 子模块保持干净；S-mode 移植位于 `trusted/` |
| dtc/libfdt | [kernel.org dtc](https://git.kernel.org/pub/scm/utils/dtc/dtc.git) | `v1.7.0` | `039a99414e778332d8f9c04cbd3072e1dcc62798` | dtc 为 GPL-2.0-or-later；libfdt 为 BSD-2-Clause | 设备树编译与解析 | 无 |
| FatFs | [Elm-Chan FatFs](https://elm-chan.org/fsw/ff/) 经 [abbrev/fatfs](https://github.com/abbrev/fatfs) 镜像 | `R0.15` | commit `b11f08931929e5f2f1fe8a3a2c0bd16d222b5625`，归档 SHA-256 `eeee66a326aaa846c772586ee1b40a601532cc43423262a07aa11ce582be5900` | FatFs license | VirtIO block 上的 FAT 文件系统 | 公开镜像归档由校验值锁定；磁盘移植层是第一方代码 |
| nanoprintf | [charlesnicholson/nanoprintf](https://github.com/charlesnicholson/nanoprintf) | main snapshot | `72cc0ba19440e891327fd7d2ef2cf871dfc4046f` | Unlicense | 有边界的格式化输出 | 无 |

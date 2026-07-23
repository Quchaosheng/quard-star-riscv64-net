# Source Migration

This repository starts with new Git history. First-party code is migrated selectively from immutable source revisions, without importing the old repositories' history or copying their bundled third-party trees.

The original source repositories are no longer publicly available as of 2026-07-23. Their names and fixed revisions are retained below as historical migration records, but those revisions cannot currently be fetched independently from the public project.

| Source | Fixed revision | Included source |
|---|---|---|
| `Quchaosheng/quard-star-riscv64-kernel` | `641f42560999ab00ad7ba01169cb2b3d723d8c48` | Boot, DTS, kernel, trusted-domain platform code, and the quard-star QEMU/OpenSBI changes |
| `Quchaosheng/tiny-tcpip-stack` | `32e4988e2d482ad3ee406e36b5adbd84a63c8e9e` | `code/pc/src/net/net`, `code/pc/src/net/src`, and selected `code/pc/src/app` modules |

The TCP/IP protocol core is migrated only from `code/pc/src/net`. The older `code/src/net`, `code/x86os-with-net`, and `chapter` trees are excluded as protocol-core sources.

The kernel and TCP/IP stack are first-party implementations owned by the project author. QEMU, OpenSBI, FreeRTOS-Kernel, dtc/libfdt, FatFs, and nanoprintf remain third-party components under their upstream licenses; see [THIRD_PARTY.md](../THIRD_PARTY.md).

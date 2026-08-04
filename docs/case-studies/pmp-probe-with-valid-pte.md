# 用合法 PTE 证明一次 PMP 拒绝

## 问题

我想证明的是：普通 S 模式内核访问 trusted RAM 时，真正挡住它的是 OpenSBI 配置的 PMP，而不是 Sv39 页表先把请求拦掉。这个区别不能靠“访问失败了”来推断，因为缺页、权限位错误、地址未映射和 PMP 拒绝都可能让一条访存指令陷入异常；如果测试只等一个 trap，得到的最多是“某处拒绝了访问”，还不足以支撑 PMP 隔离结论。

当前实现把 trusted RAM 定义为 `0xbf800000-0xbfffffff`，普通 domain 对这段区域没有权限。为了从普通内核侧做可恢复探测，我选择虚拟地址 `0x40000000`，只映射 trusted RAM 的第一页 `0xbf800000`。探测依次执行读、写和取指，并要求得到对应的 access fault。

## 错误的直觉

最容易出现的直觉是：既然目标物理页本来就不允许访问，那就不映射它，直接读 `0xbf800000`，只要崩了便算通过。问题是内核页表没有这条映射时，地址翻译阶段就可能产生 page fault，PMP 根本没有机会成为可观测的拒绝点。另一个危险做法是故意清掉 PTE 的 R、W 或 X 位，再分别执行访问；这同样是在验证页表权限，而不是 PMP。

还有一种较隐蔽的误判：trap handler 看到异常编号 1、5 或 7 就恢复执行。这样任何无关的 instruction/load/store access fault 都可能冒充探测成功，掩盖真实内核错误。只校验 `stval` 也不够，因为同一地址上的读、写、取指应当对应不同的 `scause`。

## 证据/代码路径

我核对了当前代码。`kernel/include/timeros/address.h` 定义 `PMP_TRUSTED_BASE` 为 `0xbf800000`、`PMP_PROBE_VA` 为 `0x40000000`。`kernel/src/address.c` 在 `QS_M9_PMP_TEST` 下调用 `PageTable_map()`，建立一页 `PTE_R | PTE_W | PTE_X` 的有效 Sv39 映射。也就是说，页表明确允许三类访问，VA 只用于提供稳定、独立的探测入口，最终 PA 才落到受 PMP 保护的 trusted RAM。

`kernel/src/selftest.c` 的 `m9_pmp_probe()` 在每条指令前记录预期 cause 和恢复地址：读期待 `EXC_LOAD_ACCESS`（5），写期待 `EXC_STORE_ACCESS`（7），跳转期待 `EXC_INST_ACCESS`（1）。`kernel/src/trap.c` 从 `scause` 去掉中断位后，把 cause code 与 `r_stval()` 一并交给 `m9_pmp_handle_fault()`。后者只有在探测已 armed、cause 完全一致且 `stval == 0x40000000` 时才写回恢复 PC，并把状态置为已命中；其他异常继续走通用路径，最终 panic，而不是被测试吞掉。

## 设计

我的设计核心是先消除页表这一竞争解释。R/W/X 全开的合法叶子 PTE 让地址翻译能够完成；随后读、写、取指仍分别得到 access fault，才与 PMP 拒绝相符。每次探测采用“armed、faulted”两阶段握手：执行前写入预期异常和局部标签地址，handler 命中后跳到该标签，`m9_pmp_require()` 再确认状态确实变为 faulted。

从异常编码上看，这个区分也是可检查的：当前探测期待的 instruction/load/store access fault 分别是 1、5、7，而 Sv39 翻译或 PTE 权限失败通常表现为对应的 page fault 12、13、15。测试没有把“任意同步异常”视为拒绝证据，而是把访问类型、故障地址和恢复点绑定在同一次 armed 探测上。这样即使页表构造将来被改坏，测试也会因异常类型不匹配而失败，提醒我先修复实验前提，不能继续宣称 PMP 已被验证。

这也规定了失败语义：如果访问没有被拒绝，指令会继续落到恢复标签，但状态仍是 armed，代码打印 `QS:PMP_UNTRUSTED_DENY_FAIL` 并 `panic("trusted memory access was not denied")`。因此“未发生 fault”绝不能静默通过。若 fault 的 cause 或 `stval` 不匹配，也不会伪装成成功，而会进入意外 trap 的 panic 路径。三项分别通过后才打印细分标记和汇总标记 `QS:PMP_UNTRUSTED_DENY_OK`。

## 验证边界

`tests/host/test_m8_contracts.sh` 静态检查 R/W/X 映射、三个 access-fault 常量、handler 接线和日志标记；`scripts/m8-smoke.sh` 则要求 QEMU 日志同时出现三项普通侧拒绝标记、汇总标记，以及 OpenSBI 输出中普通 domain 对 trusted RAM 无权限、trusted domain 拥有 R/W/X 的区域配置。可信侧还有反向探测，但它是另一条证据链。

这些结论明确止于 QEMU 和软件路径：它证明当前 QEMU RISC-V 模型、OpenSBI domain/PMP 配置、Sv39 页表与内核 trap 处理组合下，访问表现为匹配地址的 access fault，而不是 page fault。它不等价于真实芯片验证，也不覆盖 DMA、总线主设备、缓存侧信道、瞬态执行或 PMP 硬件实现缺陷。

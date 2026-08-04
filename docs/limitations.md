# Current Limitations

**English** | [简体中文](limitations.zh-CN.md)

- The network stack implements IPv4 only. IPv6, DHCP, TLS, HTTPS, and network offloads are not implemented.
- Guest networking uses the fixed `192.168.100.0/24` test network. Public Internet access is not part of acceptance testing.
- The TFTP client implements the tested read path and `windowsize=4`; it is not a general-purpose TFTP client or server.
- TCP is a tested subset, not a complete RFC implementation. M8 exercises the client-side path through HTTP; the dedicated M6C1/M6C2 smoke and stress stages cover the separately tested handshake, retransmission, close, listen/accept, echo, and stress paths. Payload transmission is stop-and-wait, limited to one segment of up to 512 B with a fixed 500 ms RTO; TCP options, congestion control, RTT estimation, and SACK are not implemented.
- The file syscall layer is a small FatFs test interface with four generation-checked handles, not a POSIX VFS.
- OpenSBI's domain configuration assigns PMP permissions that isolate hart7's 8 MiB trusted RAM and UART2 from harts 0-6, and isolate ordinary RAM from hart7. The load, store, and instruction access faults are tested bidirectionally and remain QEMU-only evidence; there is no physical-board, DMA-isolation, or side-channel claim.
- The trusted firmware scheduler marker is captured from UART2. There is no hardware emergency-stop or physical-device claim.
- QEMU/TAP acceptance runs on Linux or WSL2. Native Windows networking is not an acceptance environment.
- The scheduled M8 GitHub Actions job depends on hosted-runner TAP permissions and builds the patched QEMU machine from source.

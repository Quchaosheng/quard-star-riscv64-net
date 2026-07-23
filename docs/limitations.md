# Current Limitations

- The network stack implements IPv4 only. IPv6, DHCP, TLS, HTTPS, and network offloads are not implemented.
- Guest networking uses the fixed `192.168.100.0/24` test network. Public Internet access is not part of acceptance testing.
- The TFTP client implements the tested read path and `windowsize=4`; it is not a general-purpose TFTP client or server.
- The file syscall layer is a small FatFs test interface with four generation-checked handles, not a POSIX VFS.
- OpenSBI PMP isolates hart7's 8 MiB trusted RAM and UART2 from harts 0-6, and isolates ordinary RAM from hart7. The load, store, and instruction access faults are tested bidirectionally and remain QEMU-only evidence; there is no physical-board, DMA-isolation, or side-channel claim.
- The trusted firmware scheduler marker is captured from UART2. There is no hardware emergency-stop or physical-device claim.
- QEMU/TAP acceptance runs on Linux or WSL2. Native Windows networking is not an acceptance environment.
- The scheduled M8 GitHub Actions job depends on hosted-runner TAP permissions and builds the patched QEMU machine from source.

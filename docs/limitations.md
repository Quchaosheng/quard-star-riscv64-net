# Current Limitations

- The network stack implements IPv4 only. IPv6, DHCP, TLS, HTTPS, and network offloads are not implemented.
- Guest networking uses the fixed `192.168.100.0/24` test network. Public Internet access is not part of acceptance testing.
- The TFTP client implements the tested read path and `windowsize=4`; it is not a general-purpose TFTP client or server.
- The file syscall layer is a small FatFs test interface with four generation-checked handles, not a POSIX VFS.
- OpenSBI assigns hart0-6 to the ordinary domain and hart7 to the FreeRTOS domain, but both domains currently use the `allmem` region. This proves hart ownership and independent startup, not PMP-enforced memory isolation.
- The trusted firmware readiness marker is captured from UART2. There is no hardware emergency-stop or physical-device claim.
- QEMU/TAP acceptance runs on Linux or WSL2. Native Windows networking is not an acceptance environment.
- The scheduled M8 GitHub Actions job depends on hosted-runner TAP permissions and builds the patched QEMU machine from source.

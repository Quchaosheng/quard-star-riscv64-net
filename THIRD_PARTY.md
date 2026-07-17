# Third-Party Software

Third-party source keeps its upstream license. Git dependencies are pinned by the repository's gitlinks; FatFs is pinned by its official archive checksum.

| Component | Upstream | Version | Fixed revision | License | Purpose | Local changes |
|---|---|---|---|---|---|---|
| QEMU | [qemu-project/qemu](https://gitlab.com/qemu-project/qemu) | `v8.0.2` | `f7f686b61cf7ee142c9264d2e04ac2c6a96d37f8` | GPL-2.0-or-later and other licenses described upstream | quard-star machine and virtual devices | Submodule remains clean; project changes live in `patches/qemu/` |
| OpenSBI | [riscv-software-src/opensbi](https://github.com/riscv-software-src/opensbi) | `v1.2` | `6b5188ca14e59ce7bf71afe4e7d3d557c3d31bf8` | BSD-2-Clause | SBI, HSM, and domains | Submodule remains clean; project changes live in `patches/opensbi/` |
| FreeRTOS-Kernel | [FreeRTOS/FreeRTOS-Kernel](https://github.com/FreeRTOS/FreeRTOS-Kernel) | `V10.5.1` | `def7d2df2b0506d3d249334974f51e427c17a41c` | MIT | hart 7 trusted domain | Submodule remains clean; the S-mode port lives under `trusted/` |
| dtc/libfdt | [kernel.org dtc](https://git.kernel.org/pub/scm/utils/dtc/dtc.git) | `v1.7.0` | `039a99414e778332d8f9c04cbd3072e1dcc62798` | GPL-2.0-or-later for dtc; BSD-2-Clause for libfdt | Device-tree compiler and parser | None |
| FatFs | [Elm-Chan FatFs](https://elm-chan.org/fsw/ff/) | `R0.15` | SHA-256 `e0d76654d877e6c74be5ea3c395808794d495169514e98cbf6046168b8f4f070` | FatFs license | FAT filesystem over VirtIO block | Archive remains unchanged; the disk port lives in first-party code |
| nanoprintf | [charlesnicholson/nanoprintf](https://github.com/charlesnicholson/nanoprintf) | main snapshot | `72cc0ba19440e891327fd7d2ef2cf871dfc4046f` | Unlicense | Bounded formatted output | None |

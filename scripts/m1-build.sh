#!/usr/bin/env bash
set -eu

root=${QS_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
stage=${QS_STAGE:-m1}
out=$root/out/$stage
cache=${QS_BUILD_CACHE:-$HOME/.cache/quard-star-riscv64-net/m1}
sbi_dts=${QS_SBI_DTS:-$root/platform/quard-star/dts/quard_star_sbi.dts}
kernel_dts=${QS_KERNEL_DTS:-$root/platform/quard-star/dts/quard_star_kernel.dts}

prepare_tree() {
  name=$1
  upstream=$2
  patch=$3
  commit=$(git -C "$upstream" rev-parse HEAD)
  patch_id=$(sha256sum "$patch" | cut -c1-12)
  tree=$cache/$name-$commit-$patch_id

  if [ ! -d "$tree/.git" ]; then
    mkdir -p "$cache"
    git -c core.autocrlf=false clone --shared "$upstream" "$tree" >&2
    git -C "$tree" apply "$patch" >&2
  fi
  printf '%s\n' "$tree"
}

mkdir -p "$out"/{boot,disk,dts,fw,kernel,opensbi,qemu,trusted}

qemu_src=$(prepare_tree qemu "$root/third_party/qemu" \
  "$root/patches/qemu/0001-add-quard-star-machine.patch")
git -C "$qemu_src" submodule update --init --depth 1 \
  ui/keycodemapdb tests/fp/berkeley-testfloat-3 tests/fp/berkeley-softfloat-3
(
  cd "$qemu_src"
  scripts/git-submodule.sh update \
    ui/keycodemapdb tests/fp/berkeley-testfloat-3 tests/fp/berkeley-softfloat-3
  if [ ! -f build/build.ninja ]; then
    ./configure --target-list=riscv64-softmmu \
      --meson=meson --enable-fdt=system --with-git-submodules=validate \
      --disable-werror --disable-docs --disable-gtk --disable-sdl \
      --disable-opengl --disable-curses --disable-vnc
  fi
  ninja -C build qemu-system-riscv64
)
cp "$qemu_src/build/qemu-system-riscv64" "$out/qemu/"

opensbi_src=$(prepare_tree opensbi "$root/third_party/opensbi" \
  "$root/patches/opensbi/0001-add-quard-star-platform.patch")
make -C "$opensbi_src" CROSS_COMPILE=riscv64-unknown-elf- PLATFORM=quard_star
cp "$opensbi_src/build/platform/quard_star/firmware/fw_jump.bin" "$out/opensbi/"

make -C "$root/kernel" clean
make -C "$root/kernel" all
cp "$root/kernel/os.bin" "$out/kernel/"

make -C "$root/trusted" clean
make -C "$root/trusted" all
cp "$root/trusted/build/trusted_fw.bin" "$out/trusted/"

riscv64-unknown-elf-gcc -x assembler-with-cpp \
  -march=rv64imafd_zicsr_zifencei -mabi=lp64d \
  -c "$root/platform/quard-star/boot/start.s" -o "$out/boot/start.o"
riscv64-unknown-elf-gcc -nostdlib \
  -march=rv64imafd_zicsr_zifencei -mabi=lp64d \
  -T "$root/platform/quard-star/boot/boot.lds" \
  -Wl,-Map="$out/boot/lowlevel_fw.map" -Wl,--gc-sections \
  "$out/boot/start.o" -o "$out/boot/lowlevel_fw.elf"
riscv64-unknown-elf-objcopy -O binary -S \
  "$out/boot/lowlevel_fw.elf" "$out/boot/lowlevel_fw.bin"

dtc -I dts -O dtb -o "$out/dts/opensbi-domain.dtb" \
  "$sbi_dts"
dtc -I dts -O dtb -o "$out/dts/kernel.dtb" \
  "$kernel_dts"

truncate -s 32M "$out/fw/fw.bin"
dd if="$out/boot/lowlevel_fw.bin" of="$out/fw/fw.bin" conv=notrunc bs=1K seek=0 status=none
dd if="$out/dts/opensbi-domain.dtb" of="$out/fw/fw.bin" conv=notrunc bs=1K seek=512 status=none
dd if="$out/dts/kernel.dtb" of="$out/fw/fw.bin" conv=notrunc bs=1K seek=1024 status=none
dd if="$out/opensbi/fw_jump.bin" of="$out/fw/fw.bin" conv=notrunc bs=1K seek=2048 status=none
dd if="$out/trusted/trusted_fw.bin" of="$out/fw/fw.bin" conv=notrunc bs=1K seek=4096 status=none
dd if="$out/kernel/os.bin" of="$out/fw/fw.bin" conv=notrunc bs=1K seek=8192 status=none
truncate -s 64M "$out/disk/disk.img"

echo "$stage firmware: $out/fw/fw.bin"

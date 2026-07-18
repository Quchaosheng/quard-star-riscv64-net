CROSS_COMPILE = riscv64-unknown-elf-
CFLAGS = -nostdlib -fno-builtin -march=rv64imafd_zicsr_zifencei -mabi=lp64d -mcmodel=medany -g
CPPFLAGS ?=

GDB = riscv64-unknown-elf-gdb
CC = ${CROSS_COMPILE}gcc
OBJCOPY = ${CROSS_COMPILE}objcopy
OBJDUMP = ${CROSS_COMPILE}objdump

SRCS_ASM = \
	entry.S \
	kerneltrap.S \
	switch.S \
	link_app.S \

SRCS_C = \
	sbi.c \
	main.c \
	printk.c \
	trap.c \
	syscall.c \
	string.c \
	app.c \
	cpu.c \
	timer.c \
	spinlock.c \
	wait.c \
	selftest.c \
	address.c \
	task.c \
	assert.c \
	vsprintf.c \
	printf.c \
	loader.c \
	virtio_disk.c \
	bio.c \

INCLUDE := -I./include -I../third_party/dtc/libfdt
SRC := ./src
LIB := ./lib
LIBFDT := ../third_party/dtc/libfdt
OBJS = $(SRCS_ASM:.S=.o)
OBJS += $(SRCS_C:.c=.o)
OBJS += fdt.o fdt_ro.o

all: build_app os.elf

link_app.o: build_app

os.elf: ${OBJS}
	${CC} ${CFLAGS} -T os.ld -Wl,-Map=os.map -o os.elf $^
	${OBJCOPY} -O binary os.elf os.bin

%.o: $(SRC)/%.c
	${CC} ${CPPFLAGS} ${CFLAGS} $(INCLUDE) -c -o $@ $<

%.o: $(LIB)/%.c
	${CC} ${CPPFLAGS} ${CFLAGS} $(INCLUDE) -c -o $@ $<

fdt.o: $(LIBFDT)/fdt.c
	${CC} ${CPPFLAGS} ${CFLAGS} $(INCLUDE) -c -o $@ $<

fdt_ro.o: $(LIBFDT)/fdt_ro.c
	${CC} ${CPPFLAGS} ${CFLAGS} $(INCLUDE) -c -o $@ $<

%.o: $(SRC)/%.S
	${CC} ${CPPFLAGS} ${CFLAGS} $(INCLUDE) -c -o $@ $<

build_app: build.c
	@$(MAKE) -C ../user CPPFLAGS="$(CPPFLAGS)"
	@gcc $< -o build.out
	./build.out
	# NTFS timestamps can briefly appear ahead of WSL's clock.
	touch -d '1 second ago' src/link_app.S

.PHONY: debug clean

debug:
	@echo "os debug start..."
	@${GDB} os.elf -q -x ./gdbinit

clean:
	rm -rf *.o *.bin *.out *.map *.elf
	rm -rf ../user/bin/*

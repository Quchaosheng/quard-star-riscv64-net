CROSS_COMPILE = riscv64-unknown-elf-
CFLAGS = -nostdlib -fno-builtin -march=rv64imafd_zicsr_zifencei -mabi=lp64d -mcmodel=medany

CC = ${CROSS_COMPILE}gcc
OBJCOPY = ${CROSS_COMPILE}objcopy
OBJDUMP = ${CROSS_COMPILE}objdump
INCLUDE:=-I../kernel/include

LIB = ../kernel/lib
BIN_DIR = bin


.PHONY: all prepare-bin

all: initproc xec user_shell time

prepare-bin:
	mkdir -p $(BIN_DIR)

write: write.c $(LIB)/*.c | prepare-bin
	${CC} ${CFLAGS} $(INCLUDE) -T user.ld $^ -o bin/write

time: time.c $(LIB)/*.c | prepare-bin
	${CC} ${CFLAGS} $(INCLUDE) -T user.ld $^ -o bin/time

user_shell:user_shell.c $(LIB)/*.c | prepare-bin
	${CC} ${CFLAGS} $(INCLUDE) -T user.ld  $^ -o bin/user_shell

xec: xec.c $(LIB)/*.c | prepare-bin
	${CC} ${CFLAGS} $(INCLUDE) -T user.ld  $^ -o bin/xec

initproc: initproc.c $(LIB)/*.c | prepare-bin
	${CC} ${CFLAGS} $(INCLUDE) -T user.ld $^ -o bin/initproc

debug: objdump_time objdump_write objdump_xec

objdump_time:
	${OBJDUMP} -d bin/time > time.txt
objdump_write:
	${OBJDUMP} -d bin/write > write.txt
objdump_xec:
	${OBJDUMP} -d bin/xec > xec.txt

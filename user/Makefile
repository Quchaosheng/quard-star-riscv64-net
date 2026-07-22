CROSS_COMPILE = riscv64-unknown-elf-
CFLAGS = -nostdlib -fno-builtin -march=rv64imafd_zicsr_zifencei -mabi=lp64d -mcmodel=medany
CPPFLAGS ?=

CC = ${CROSS_COMPILE}gcc
OBJCOPY = ${CROSS_COMPILE}objcopy
OBJDUMP = ${CROSS_COMPILE}objdump
INCLUDE := -I../kernel/include
LIB := ../kernel/lib
BIN_DIR := bin

.PHONY: all prepare-bin debug objdump_time objdump_write objdump_xec

all: initproc xec user_shell time udp_echo tcp_echo tcp_server_echo m7a_dns_echo m7b_http_get m7c_ntp_get m7d_tftp_get m7e_tftp_get

prepare-bin:
	mkdir -p $(BIN_DIR)

write: write.c $(LIB)/*.c | prepare-bin
	${CC} ${CPPFLAGS} ${CFLAGS} $(INCLUDE) -T user.ld $^ -o bin/write

time: time.c $(LIB)/*.c | prepare-bin
	${CC} ${CPPFLAGS} ${CFLAGS} $(INCLUDE) -T user.ld $^ -o bin/time

user_shell: user_shell.c $(LIB)/*.c | prepare-bin
	${CC} ${CPPFLAGS} ${CFLAGS} $(INCLUDE) -T user.ld $^ -o bin/user_shell

xec: xec.c $(LIB)/*.c | prepare-bin
	${CC} ${CPPFLAGS} ${CFLAGS} $(INCLUDE) -T user.ld $^ -o bin/xec

initproc: initproc.c $(LIB)/*.c | prepare-bin
	${CC} ${CPPFLAGS} ${CFLAGS} $(INCLUDE) -T user.ld $^ -o bin/initproc

udp_echo: udp_echo.c $(LIB)/*.c | prepare-bin
	${CC} ${CPPFLAGS} ${CFLAGS} $(INCLUDE) -T user.ld $^ -o bin/udp_echo

tcp_echo: tcp_echo.c $(LIB)/*.c | prepare-bin
	${CC} ${CPPFLAGS} ${CFLAGS} $(INCLUDE) -T user.ld $^ -o bin/tcp_echo

tcp_server_echo: tcp_server_echo.c $(LIB)/*.c | prepare-bin
	${CC} ${CPPFLAGS} ${CFLAGS} $(INCLUDE) -T user.ld $^ -o bin/tcp_server_echo

m7a_dns_echo: m7a_dns_echo.c $(LIB)/*.c | prepare-bin
	${CC} ${CPPFLAGS} ${CFLAGS} $(INCLUDE) -T user.ld $^ -o bin/m7a_dns_echo

m7b_http_get: m7b_http_get.c http.c $(LIB)/*.c | prepare-bin
	${CC} ${CPPFLAGS} ${CFLAGS} $(INCLUDE) -T user.ld $^ -o bin/m7b_http_get

m7c_ntp_get: m7c_ntp_get.c ntp.c $(LIB)/*.c | prepare-bin
	${CC} ${CPPFLAGS} ${CFLAGS} $(INCLUDE) -T user.ld $^ -o bin/m7c_ntp_get

m7d_tftp_get: m7d_tftp_get.c tftp.c $(LIB)/*.c | prepare-bin
	${CC} ${CPPFLAGS} ${CFLAGS} $(INCLUDE) -T user.ld $^ -o bin/m7d_tftp_get

m7e_tftp_get: m7e_tftp_get.c tftp.c sha256.c $(LIB)/*.c | prepare-bin
	${CC} ${CPPFLAGS} ${CFLAGS} $(INCLUDE) -T user.ld $^ -o bin/m7e_tftp_get

debug: objdump_time objdump_write objdump_xec

objdump_time:
	${OBJDUMP} -d bin/time > time.txt

objdump_write:
	${OBJDUMP} -d bin/write > write.txt

objdump_xec:
	${OBJDUMP} -d bin/xec > xec.txt

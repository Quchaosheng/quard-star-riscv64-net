#ifndef TOS_TYPES_H__
#define TOS_TYPES_H__

// 定义无符号整型
#include <stddef.h>
#include <stdint.h>

typedef uint64_t u64;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint8_t u8;

/*
 * RISCV64: 寄存器的大小是64位的
 */
typedef uint64_t reg_t;


#define EOF -1
#ifndef NULL
#define NULL ((void *)0)
#endif
#define EOS '\0'

#ifndef __cplusplus
#define bool _Bool
#define true 1
#define false 0
#endif

#endif

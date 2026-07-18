#ifndef TOS_NET_DBG_H__
#define TOS_NET_DBG_H__

#include <timeros/net/net_port.h>

#ifdef __riscv
#include <timeros/os.h>
#define dbg_assert(expr, msg) { if (!(expr)) panic(msg); }
#else
#define dbg_assert(expr, msg) { assert(expr); }
#endif

#define dbg_error(module, fmt, ...) ((void)0)
#define dbg_warning(module, fmt, ...) ((void)0)
#define dbg_info(module, fmt, ...) ((void)0)
#define DBG_DISP_ENABLED(module) 0

#endif

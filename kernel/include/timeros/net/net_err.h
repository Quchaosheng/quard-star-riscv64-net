#ifndef TOS_NET_ERR_H__
#define TOS_NET_ERR_H__

typedef enum {
    NET_ERR_OK = 0,
    NET_ERR_SYS = -1,
    NET_ERR_MEM = -2,
    NET_ERR_FULL = -3,
    NET_ERR_TMO = -4,
    NET_ERR_NONE = -5,
    NET_ERR_SIZE = -6,
    NET_ERR_PARAM = -7,
} net_err_t;

#endif

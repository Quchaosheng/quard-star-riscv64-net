#ifndef TOS_NET_CFG_H__
#define TOS_NET_CFG_H__

#define PKTBUF_BLK_SIZE 1024
#define PKTBUF_BLK_CNT 256
#define PKTBUF_BUF_CNT 128
#define NET_ENDIAN_LITTLE 1

#define NETIF_HWADDR_SIZE 10
#define NETIF_NAME_SIZE 16
#define NETIF_DEV_CNT 4
#define NETIF_INQ_SIZE 16
#define NETIF_OUTQ_SIZE 16
#define TIMER_NAME_SIZE 32
#ifndef ARP_CACHE_SIZE
#define ARP_CACHE_SIZE 8
#endif
#ifndef ARP_MAX_PKT_WAIT
#define ARP_MAX_PKT_WAIT 8
#endif
#ifndef ARP_ENTRY_STABLE_TMO
#define ARP_ENTRY_STABLE_TMO 1200
#endif
#ifndef ARP_ENTRY_PENDING_TMO
#define ARP_ENTRY_PENDING_TMO 3
#endif
#ifndef ARP_ENTRY_RETRY_CNT
#define ARP_ENTRY_RETRY_CNT 5
#endif
#ifndef ARP_TIMER_TMO
#define ARP_TIMER_TMO 1
#endif

#if ARP_ENTRY_STABLE_TMO <= 0
#error "ARP_ENTRY_STABLE_TMO must be positive"
#endif
#if ARP_ENTRY_PENDING_TMO <= 0
#error "ARP_ENTRY_PENDING_TMO must be positive"
#endif
#if ARP_ENTRY_RETRY_CNT <= 0
#error "ARP_ENTRY_RETRY_CNT must be positive"
#endif
#if ARP_TIMER_TMO <= 0
#error "ARP_TIMER_TMO must be positive"
#endif

#endif

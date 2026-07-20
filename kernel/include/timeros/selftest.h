#ifndef TOS_SELFTEST_H__
#define TOS_SELFTEST_H__

void m2c_selftest_init(void);
void m2c_mark_alloc(void);
void m2c_mark_wait(void);
void m2c_mark_ipi(void);
void m2c_mark_rfence(void);
void m2c_mark_sched(void);
void m2c_selftest_poll(void);
void m3_mark_virtqueue(void);
void m3_mark_block_irq(void);
void m3_mark_block_stress(void);
void m3_mark_fatfs(void);
void m4_mark_net_link(void);
void m4_mark_net_irq(void);
void m4_mark_net_tx(void);
void m4_mark_net_rx(void);
void m4_mark_net_reset(void);
void m4_mark_net_stress(void);
void m5_mark_net_arp(void);
void m5_mark_net_ping(void);
void m6_mark_queue(void);
void m6_mark_arp_timer(void);
void m6_mark_loop(void);
void m6b_mark_udp(void);
void m6b_mark_udp_timeout(void);
void m6c1_mark_tcp(void);
void m6c1_mark_tcp_retrans(void);
void m6c1_mark_tcp_close(void);

#endif

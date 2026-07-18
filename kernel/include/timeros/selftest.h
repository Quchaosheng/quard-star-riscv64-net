#ifndef TOS_SELFTEST_H__
#define TOS_SELFTEST_H__

void m2c_selftest_init(void);
void m2c_mark_alloc(void);
void m2c_mark_wait(void);
void m2c_mark_ipi(void);
void m2c_mark_rfence(void);
void m2c_mark_sched(void);
void m2c_selftest_poll(void);

#endif

#ifndef TOS_SBI_H__
#define TOS_SBI_H__

enum sbi_ext_id {
	SBI_EXT_0_1_SET_TIMER = 0x0,
	SBI_EXT_0_1_CONSOLE_PUTCHAR = 0x1,
	SBI_EXT_0_1_CONSOLE_GETCHAR = 0x2,
	SBI_EXT_0_1_CLEAR_IPI = 0x3,
	SBI_EXT_0_1_SEND_IPI = 0x4,
	SBI_EXT_0_1_REMOTE_FENCE_I = 0x5,
	SBI_EXT_0_1_REMOTE_SFENCE_VMA = 0x6,
	SBI_EXT_0_1_REMOTE_SFENCE_VMA_ASID = 0x7,
	SBI_EXT_0_1_SHUTDOWN = 0x8,
	SBI_EXT_BASE = 0x10,
	SBI_EXT_TIME = 0x54494D45,
	SBI_EXT_IPI = 0x735049,
	SBI_EXT_RFENCE = 0x52464E43,
	SBI_EXT_HSM = 0x48534D,
	SBI_EXT_SRST = 0x53525354,
	SBI_EXT_PMU = 0x504D55,
};

enum sbi_ext_time_fid {
	SBI_EXT_TIME_SET_TIMER = 0,
};

#define SBI_FID_SET_TIMER		SBI_EXT_TIME_SET_TIMER
/* sbi 返回结构体*/
struct sbiret {
	long error;
	long value;
};

enum sbi_ext_hsm_fid {
	SBI_EXT_HSM_HART_START = 0,
	SBI_EXT_HSM_HART_STOP = 1,
	SBI_EXT_HSM_HART_GET_STATUS = 2,
};

enum sbi_ext_ipi_fid {
	SBI_EXT_IPI_SEND_IPI = 0,
};

enum sbi_ext_rfence_fid {
	SBI_EXT_RFENCE_REMOTE_SFENCE_VMA = 1,
};

struct sbiret sbi_ecall(int ext, int fid, unsigned long arg0,
			unsigned long arg1, unsigned long arg2,
			unsigned long arg3, unsigned long arg4,
			unsigned long arg5);
void sbi_console_putchar(int ch);
int sbi_console_getchar(void);
struct sbiret sbi_hart_start(u64 hartid, u64 start_addr, u64 opaque);
struct sbiret sbi_hart_get_status(u64 hartid);
struct sbiret sbi_send_ipi(u64 hart_mask, u64 hart_mask_base);
struct sbiret sbi_remote_sfence_vma(u64 hart_mask, u64 hart_mask_base,
				    u64 start, u64 size);



#endif

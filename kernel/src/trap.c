#include <timeros/os.h>

#define SCAUSE_INTERRUPT_MASK (1ULL << 63)
#define SCAUSE_CODE_MASK      0xfffULL

#define IRQ_S_TIMER 5
#define IRQ_S_EXT   9
#define EXC_U_ECALL 8

static int handle_interrupt(reg_t scause, int from_user)
{
	reg_t cause_code = scause & SCAUSE_CODE_MASK;
	if ((scause & SCAUSE_INTERRUPT_MASK) == 0) {
		return 0;
	}
	switch (cause_code) {
	case IRQ_S_TIMER:
		set_next_trigger();
		if (from_user) {
			schedule();
		}
		return 1;
	case IRQ_S_EXT:
		virtio_disk_intr();
		return 1;
	default:
		printk("undefined interrupt scause:%lx\n", scause);
		return -1;
	}
}

void set_kernel_trap_entry()
{
	w_stvec((reg_t)kernelvec);
}

void set_user_trap_entry()
{
	w_stvec((reg_t)TRAMPOLINE);
}

void kerneltrap()
{
	reg_t sepc = r_sepc();
	reg_t sstatus = r_sstatus();
	reg_t scause = r_scause();

	if ((sstatus & SSTATUS_SPP) == 0) {
		panic("kerneltrap: not from supervisor mode");
	}
	if (handle_interrupt(scause, 0) <= 0) {
		panic("kerneltrap: unexpected trap");
	}

	w_sepc(sepc);
	w_sstatus(sstatus);
}

void trap_handler()
{
	set_kernel_trap_entry();
	TrapContext* cx = (TrapContext*)get_current_trap_cx();
	reg_t scause = r_scause();
	reg_t cause_code = scause & SCAUSE_CODE_MASK;
	int intr = handle_interrupt(scause, 1);
	if (intr > 0) {
		trap_return();
		return;
	}
	if (intr < 0) {
		exit_current_and_run_next(-1);
	}

	switch (cause_code) {
	case EXC_U_ECALL: {
		cx->sepc += 4;
		reg_t result = __SYSCALL(cx->a7, cx->a0, cx->a1, cx->a2);
		cx = (TrapContext*)get_current_trap_cx();
		cx->a0 = result;
		break;
	}
	default:
		printk("undefined exception scause:%lx sepc:%lx stval:%lx\n",
		       scause, r_sepc(), r_stval());
		exit_current_and_run_next(-1);
		break;
	}

	trap_return();
}

void trap_return()
{
	set_user_trap_entry();
	u64 trap_cx_ptr = TRAPFRAME;
	u64 user_satp = current_user_token();
	u64 restore_va = (u64)__restore - (u64)__alltraps + TRAMPOLINE;
	asm volatile (
		"fence.i\n\t"
		"mv a0, %0\n\t"
		"mv a1, %1\n\t"
		"jr %2\n\t"
		:
		: "r" (trap_cx_ptr),
		  "r" (user_satp),
		  "r" (restore_va)
		: "a0", "a1"
	);
}

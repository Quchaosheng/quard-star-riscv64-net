#include <FreeRTOS.h>
#include <task.h>
#include "debug_log.h"
#include "sbi.h"
#include "riscv_asm.h"
#include "queue.h"
//消息队列控制权柄
QueueHandle_t xMyQueueHandle;

static volatile uintptr_t pmp_probe_state;
static uintptr_t pmp_probe_cause;
static uintptr_t pmp_probe_address;
static uintptr_t pmp_probe_resume;

void freertos_risc_v_application_exception_handler(uintptr_t cause,
                                                     uintptr_t *saved_pc)
{
    if (pmp_probe_state == 1 && cause == pmp_probe_cause &&
        csr_read(CSR_STVAL) == pmp_probe_address) {
        *saved_pc = pmp_probe_resume;
        pmp_probe_state = 2;
        return;
    }

    switch (cause) {
    case 1:
        _puts("QS:TRUSTED_EXCEPTION:INSTRUCTION_ACCESS\n");
        break;
    case 2:
        _puts("QS:TRUSTED_EXCEPTION:ILLEGAL_INSTRUCTION\n");
        break;
    case 5:
        _puts("QS:TRUSTED_EXCEPTION:LOAD_ACCESS\n");
        break;
    case 7:
        _puts("QS:TRUSTED_EXCEPTION:STORE_ACCESS\n");
        break;
    default:
        _puts("QS:TRUSTED_EXCEPTION:OTHER\n");
        break;
    }
    for (;;)
        __asm volatile("wfi");
}

static void pmp_probe_arm(uintptr_t cause, uintptr_t address, void *resume)
{
    pmp_probe_cause = cause;
    pmp_probe_address = address;
    pmp_probe_resume = (uintptr_t)resume;
    pmp_probe_state = 1;
    __asm volatile("" ::: "memory");
}

static void pmp_probe_require(char *marker)
{
    __asm volatile("" ::: "memory");
    if (pmp_probe_state != 2) {
        _puts("QS:PMP_TRUSTED_DENY_FAIL\n");
        for (;;)
            __asm volatile("wfi");
    }
    _puts(marker);
    _puts("\n");
}

void freertos_risc_v_application_interrupt_handler(uintptr_t cause)
{
    (void)cause;
    _puts("QS:TRUSTED_INTERRUPT:UNEXPECTED\n");
    for (;;)
        __asm volatile("wfi");
}

static void acceptance_task(void *arg)
{
    (void)arg;
    _puts("QS:TRUSTED_FIRST_TASK\n");
    vTaskDelay(pdMS_TO_TICKS(50));
    _puts("QS:TRUSTED_SCHED_OK\n");

    pmp_probe_arm(5, 0x80200000UL, &&resume_load);
    __asm volatile("lw zero, 0(%0)" ::
                   "r"((uintptr_t)0x80200000UL) : "memory");
resume_load:
    pmp_probe_require("QS:PMP_TRUSTED_LOAD_DENY_OK");

    pmp_probe_arm(7, 0x80200000UL, &&resume_store);
    __asm volatile("sw zero, 0(%0)" ::
                   "r"((uintptr_t)0x80200000UL) : "memory");
resume_store:
    pmp_probe_require("QS:PMP_TRUSTED_STORE_DENY_OK");

    pmp_probe_arm(1, 0x80200000UL, &&resume_exec);
    __asm volatile("jr %0" :: "r"((uintptr_t)0x80200000UL) : "memory");
resume_exec:
    pmp_probe_require("QS:PMP_TRUSTED_EXEC_DENY_OK");
    _puts("QS:PMP_TRUSTED_DENY_OK\n");
    vTaskDelete(NULL);
}

void task1(void *p_arg)
{
    int time = 1;
    for(;;)
    {
        _puts("task1 send\n");
        xQueueSend(xMyQueueHandle,&time,0);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void task2(void *p_arg)
{
    int time = 2;
    for(;;)
    {
        _puts("task2 send\n");
        xQueueSend(xMyQueueHandle,&time,0);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void task3(void *p_arg)
{
    int time = 0;
    for(;;)
    {
      // _puts("task3\n");

        xQueueReceive(xMyQueueHandle,&time,portMAX_DELAY);
        if(time == 1){
            _puts("task1 receive\n");
        }
        if(time == 2){
            _puts("task2 receive\n");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void vTaskCreate ()
{

    xTaskCreate(acceptance_task,"accept",512,NULL,6,NULL);
    xTaskCreate(task1,"task1",1024,NULL,3,NULL);
    xTaskCreate(task2,"task2",1024,NULL,4,NULL);
    xTaskCreate(task3,"task2",1024,NULL,5,NULL);
}

int main( void )
{
	_puts( "QS:TRUSTED_READY\n");
    xMyQueueHandle = xQueueCreate(20,sizeof(int32_t));

    vTaskCreate();
	vTaskStartScheduler();
	return 0;
}

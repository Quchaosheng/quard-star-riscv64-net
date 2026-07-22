#include <FreeRTOS.h>
#include <task.h>
#include "debug_log.h"
#include "sbi.h"
#include "riscv_asm.h"
#include "queue.h"
//消息队列控制权柄
QueueHandle_t xMyQueueHandle;

static void acceptance_task(void *arg);

void trusted_trace_task_context(const uintptr_t *context)
{
    uintptr_t status = context[30];

    if (context[0] == (uintptr_t)acceptance_task)
        _puts("QS:TRUSTED_TASK_PC_ACCEPT\n");
    else
        _puts("QS:TRUSTED_TASK_PC_BAD\n");
    if ((status & 0x122UL) == 0x120UL)
        _puts("QS:TRUSTED_TASK_STATUS_OK\n");
    else
        _puts("QS:TRUSTED_TASK_STATUS_BAD\n");
}

void freertos_risc_v_application_exception_handler(uintptr_t cause)
{
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

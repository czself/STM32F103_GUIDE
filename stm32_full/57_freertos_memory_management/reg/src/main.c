#include "stm32f1xx.h"
#include "FreeRTOS.h"
#include "task.h"

/*
 * 寄存器版：FreeRTOS 内存与任务栈水位。
 *
 * 本课不新增队列、信号量或软件定时器，重点是看清：
 * - xTaskCreate() 会从 FreeRTOS heap 分配 TCB 和任务栈
 * - 每个任务有自己的 stack，不是所有任务共用 main 栈
 * - uxTaskGetStackHighWaterMark(NULL) 可以观察当前任务历史剩余栈空间
 */

static volatile UBaseType_t g_stack_watermark;

static void system_clock_72mhz_init(void)
{
    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;

    RCC->CR |= RCC_CR_HSEON;
    while ((RCC->CR & RCC_CR_HSERDY) == 0U) {
    }

    RCC->CFGR &= ~(RCC_CFGR_HPRE |
                   RCC_CFGR_PPRE1 |
                   RCC_CFGR_PPRE2 |
                   RCC_CFGR_PLLSRC |
                   RCC_CFGR_PLLXTPRE |
                   RCC_CFGR_PLLMULL |
                   RCC_CFGR_SW);
    RCC->CFGR |= RCC_CFGR_HPRE_DIV1;
    RCC->CFGR |= RCC_CFGR_PPRE1_DIV2;
    RCC->CFGR |= RCC_CFGR_PPRE2_DIV1;
    RCC->CFGR |= RCC_CFGR_PLLSRC;
    RCC->CFGR |= RCC_CFGR_PLLMULL9;

    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0U) {
    }

    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {
    }
}

static void gpio_init(void)
{
    /* PC13 用来证明 memory_task 仍在周期运行。 */
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN |
                    RCC_APB2ENR_IOPAEN |
                    RCC_APB2ENR_AFIOEN;

    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
    GPIOC->CRH |= GPIO_CRH_MODE13_1;
    GPIOC->BSRR = GPIO_BSRR_BS13;

    GPIOA->CRL &= ~(GPIO_CRL_MODE0 |
                    GPIO_CRL_CNF0 |
                    GPIO_CRL_MODE1 |
                    GPIO_CRL_CNF1 |
                    GPIO_CRL_MODE2 |
                    GPIO_CRL_CNF2);
    GPIOA->CRL |= GPIO_CRL_CNF0_1;
    GPIOA->CRL |= GPIO_CRL_MODE1_1;
    GPIOA->CRL |= GPIO_CRL_MODE2_1;
    GPIOA->BSRR = GPIO_BSRR_BS0;
}

static void led_toggle_pc13(void)
{
    if ((GPIOC->ODR & GPIO_ODR_ODR13) != 0U) {
        GPIOC->BRR = GPIO_BRR_BR13;
    } else {
        GPIOC->BSRR = GPIO_BSRR_BS13;
    }
}

static void stop_for_debug(void)
{
    taskDISABLE_INTERRUPTS();
    while (1) {
    }
}

void vApplicationMallocFailedHook(void)
{
    /*
     * heap 不够时，任务 TCB、任务栈、队列、定时器等对象都可能分配失败。
     * 本课只有一个任务；如果仍进这里，优先查 configTOTAL_HEAP_SIZE 或栈深度。
     */
    stop_for_debug();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;

    /*
     * 栈溢出不是 heap 用完，而是某个任务自己的 stack 被写穿。
     * 关中断停住后，可以在调试器里看 task_name 和调用栈定位哪个任务出问题。
     */
    stop_for_debug();
}

static void memory_task(void *arg)
{
    (void)arg;

    while (1) {
        /*
         * NULL 表示查询“当前正在运行的任务”，也就是 memory_task 自己。
         * 返回值是历史最低剩余栈空间，通常以 StackType_t word 为单位，
         * 不能简单当作字节数。数值越小，说明越接近栈溢出。
         */
        g_stack_watermark = uxTaskGetStackHighWaterMark(NULL);

        led_toggle_pc13();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

int main(void)
{
    system_clock_72mhz_init();
    gpio_init();

    /*
     * 160 是 memory_task 的任务栈深度。
     * 创建任务时，FreeRTOS 会从 heap 中分配 TCB 和这块任务栈；
     * 创建成功后再用 g_stack_watermark 观察这个估算是否留有余量。
     */
    BaseType_t memory_ok = xTaskCreate(memory_task,
                                       "mem",
                                       160,
                                       NULL,
                                       1,
                                       NULL);

    if (memory_ok != pdPASS) {
        stop_for_debug();
    }

    vTaskStartScheduler();

    stop_for_debug();
}

#include "stm32f1xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/*
 * 寄存器版：FreeRTOS 中断与临界区。
 *
 * 48 之前的任务都在任务上下文里互相调度。本课第一次把硬件中断接进来：
 * PA0 下降沿 -> EXTI0_IRQHandler -> xQueueSendFromISR() -> event_task 被唤醒。
 *
 * 本课新重点：
 * - ISR 里使用 FromISR API，不能使用会阻塞的普通队列 API
 * - EXTI0 优先级必须满足 FreeRTOS 的中断优先级约束
 * - ISR 只投递事件，业务处理放到任务里
 * - 临界区只保护短小的共享变量修改
 */

static QueueHandle_t g_queue;
static volatile uint32_t g_shared;

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
    /*
     * PC13 是任务处理完事件后的可见证据。
     * PA0 是 EXTI0 输入，上拉后按下接地形成下降沿。
     */
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

static void exti0_pa0_init(void)
{
    /*
     * AFIO_EXTICR[0] 选择 EXTI0 的输入来源。
     * 清成 0 表示 EXTI0 来自 PA0，而不是 PB0/PC0 等其他端口。
     */
    AFIO->EXTICR[0] &= ~AFIO_EXTICR1_EXTI0;

    /*
     * IMR 放行 EXTI0 中断请求；FTSR 选择下降沿触发。
     * 本课 PA0 上拉，按下接地，从 1 变 0，正好是下降沿。
     */
    EXTI->IMR |= EXTI_IMR_MR0;
    EXTI->FTSR |= EXTI_FTSR_TR0;

    /*
     * FreeRTOSConfig.h 中 configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5。
     * Cortex-M 数值越小优先级越高；调用 FromISR API 的中断要用 5 或更大的数值。
     * 所以这里用 6，避免 EXTI0 优先级高到不能安全调用 FreeRTOS API。
     */
    NVIC_SetPriority(EXTI0_IRQn, 6);
    NVIC_EnableIRQ(EXTI0_IRQn);
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
    /* 队列、任务 TCB 或任务栈从 FreeRTOS heap 分配失败时会进入这里。 */
    stop_for_debug();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    /* event_task 栈不足时会进入这里；可在调试器里查看 task_name。 */
    (void)task;
    (void)task_name;
    stop_for_debug();
}

static void event_task(void *arg)
{
    uint8_t event_value;

    (void)arg;

    while (1) {
        /*
         * 队列为空时，event_task 阻塞在这里，不占 CPU。
         * EXTI0 ISR 发送事件后，队列唤醒本任务。
         */
        if (xQueueReceive(g_queue, &event_value, portMAX_DELAY) == pdPASS) {
            /*
             * g_shared++ 是读-改-写序列，不是教学上可随便打断的“一个动作”。
             * 临界区只包住这一小段，避免扩大关中断/禁止调度的时间。
             */
            taskENTER_CRITICAL();
            g_shared++;
            taskEXIT_CRITICAL();

            led_toggle_pc13();
        }
    }
}

void EXTI0_IRQHandler(void)
{
    BaseType_t higher_priority_woken = pdFALSE;

    if ((EXTI->PR & EXTI_PR_PR0) != 0U) {
        uint8_t event_value = 1U;

        /*
         * 写 1 清 EXTI pending 标志。
         * 如果不清，退出 ISR 后可能立刻再次进入中断。
         */
        EXTI->PR = EXTI_PR_PR0;

        /*
         * ISR 里必须使用 FromISR 版本。
         * 第三个参数告诉内核：这次发送是否唤醒了更高优先级任务。
         */
        (void)xQueueSendFromISR(g_queue, &event_value, &higher_priority_woken);
    }

    /*
     * 如果队列发送唤醒了更高优先级任务，这里请求在中断退出时立即切换。
     * 否则任务可能要等到下一次 tick 才运行，响应会变慢。
     */
    portYIELD_FROM_ISR(higher_priority_woken);
}

int main(void)
{
    system_clock_72mhz_init();
    gpio_init();
    exti0_pa0_init();

    /*
     * 队列长度 4，每个元素 1 字节。
     * ISR 发送的是事件值副本，局部变量离开 ISR 后也不会影响队列内容。
     */
    g_queue = xQueueCreate(4, sizeof(uint8_t));

    BaseType_t task_ok = xTaskCreate(event_task, "evt", 160, NULL, 2, NULL);

    if ((g_queue == NULL) || (task_ok != pdPASS)) {
        stop_for_debug();
    }

    vTaskStartScheduler();

    stop_for_debug();
}

#include "stm32f1xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

/*
 * 寄存器版：FreeRTOS 软件定时器。
 *
 * 本课的重点不是 STM32 TIMx 外设。
 * 软件定时器由 FreeRTOS 根据 tick 维护到期时间，到期后由 timer service task
 * 调用回调函数。PC13 的 500ms 翻转来自 timer_cb()，PA1 的 1000ms 翻转来自
 * 普通 monitor 任务，用两个节奏区分“定时器回调”和“普通任务循环”。
 */

static TimerHandle_t g_timer;

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
    /* GPIO 已在前面多课反复使用，这里只保留和现象对应的必要注释。 */
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

static void led_toggle_pa1(void)
{
    if ((GPIOA->ODR & GPIO_ODR_ODR1) != 0U) {
        GPIOA->BRR = GPIO_BRR_BR1;
    } else {
        GPIOA->BSRR = GPIO_BSRR_BS1;
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
     * 软件定时器对象、timer service task、monitor 任务的 TCB/栈都要占用
     * FreeRTOS heap。停在这里通常说明 heap 太小或对象创建太多。
     */
    stop_for_debug();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;

    /*
     * timer callback 虽然不是独立任务，但它运行在 timer service task 中。
     * 如果回调里加入大局部变量或复杂调用，也可能把定时器服务任务栈压爆。
     */
    stop_for_debug();
}

static void timer_cb(TimerHandle_t timer)
{
    (void)timer;

    /*
     * 回调运行在 FreeRTOS timer service task 上，不是硬件中断。
     * 因此这里不用 FromISR API；但也不能写长延时或等待队列，否则会拖住
     * 其他软件定时器的启动、停止和到期回调。
     */
    led_toggle_pc13();
}

static void monitor_task(void *arg)
{
    (void)arg;

    while (1) {
        led_toggle_pa1();

        /* PA1 的慢节奏证明普通任务调度仍在运行。 */
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

int main(void)
{
    system_clock_72mhz_init();
    gpio_init();

    /*
     * xTimerCreate() 只创建软件定时器对象，还不会开始计时。
     *
     * 参数依次表示：
     * - "blink"：给调试看的名字
     * - 500ms：到期周期，最终由 FreeRTOS tick 换算
     * - pdTRUE：自动重装，到期后继续安排下一次 500ms
     * - NULL：本课不需要 timer ID 区分多个定时器
     * - timer_cb：到期后由 timer service task 调用的函数
     */
    g_timer = xTimerCreate("blink",
                           pdMS_TO_TICKS(500),
                           pdTRUE,
                           NULL,
                           timer_cb);

    BaseType_t monitor_ok = xTaskCreate(monitor_task,
                                        "mon",
                                        128,
                                        NULL,
                                        1,
                                        NULL);

    if ((g_timer == NULL) || (monitor_ok != pdPASS)) {
        stop_for_debug();
    }

    /*
     * xTimerStart() 不是直接在 main 里执行回调，而是把“启动定时器”命令
     * 放进 timer command queue。第二个参数为 0，表示队列满时不等待。
     */
    BaseType_t timer_start_ok = xTimerStart(g_timer, 0);

    if (timer_start_ok != pdPASS) {
        stop_for_debug();
    }

    vTaskStartScheduler();

    stop_for_debug();
}

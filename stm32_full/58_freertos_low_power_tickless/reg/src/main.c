#include "stm32f1xx.h"
#include "FreeRTOS.h"
#include "task.h"

/*
 * 寄存器版：FreeRTOS Tickless Idle。
 *
 * 本课不是 STM32 Stop/Standby 低功耗工程。
 * 代码没有配置 PWR、RTC、EXTI 唤醒，也没有睡醒后重配 PLL。
 * 它只演示 FreeRTOS 在长时间空闲时调用 vApplicationSleep()，应用层在里面
 * 执行 Cortex-M 的 __WFI()，等待下一次中断唤醒。
 */

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
    /* PC13 用来观察 slow_task 是否每 2 秒被唤醒继续运行。 */
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
     * tickless 本身不创建业务对象，但调度器至少要创建 idle task，
     * slow_task 也需要 TCB 和任务栈；heap 不够会停在这里。
     */
    stop_for_debug();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;
    stop_for_debug();
}

void vApplicationSleep(TickType_t expected_idle_time)
{
    /*
     * FreeRTOS 在预计空闲时间足够长时调用这个函数。
     * expected_idle_time 是预计可以睡眠的 tick 数，本课只做最小演示，
     * 不根据它切换 Stop/Standby，也不改外设时钟。
     */
    (void)expected_idle_time;

    /*
     * __WFI() 是 Cortex-M Wait For Interrupt 指令。
     * CPU 会等到 SysTick 或其他中断再继续；这不是 STM32 深睡眠配置。
     */
    __WFI();
}

static void slow_task(void *arg)
{
    (void)arg;

    while (1) {
        led_toggle_pc13();

        /*
         * 2 秒阻塞制造较长 idle 窗口。
         * 如果没有任务长时间阻塞，tickless idle 就没有明显触发机会。
         */
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

int main(void)
{
    system_clock_72mhz_init();
    gpio_init();

    BaseType_t slow_ok = xTaskCreate(slow_task,
                                     "slow",
                                     128,
                                     NULL,
                                     1,
                                     NULL);

    if (slow_ok != pdPASS) {
        stop_for_debug();
    }

    /*
     * tickless 是调度器启动后的 idle 路径行为。
     * main 里直接 while 不会触发本课的 vApplicationSleep() 链路。
     */
    vTaskStartScheduler();

    stop_for_debug();
}

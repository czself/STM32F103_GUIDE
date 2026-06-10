#include "stm32f1xx.h"
#include "FreeRTOS.h"
#include "task.h"

/*
 * 寄存器版：调度器与时间片。
 *
 * 46 已经学过任务可以被挂起。本课看任务都可运行时，调度器怎样选择：
 * - high_task 优先级 2，醒来时优先运行
 * - same_a / same_b 同为优先级 1，在 high_task 阻塞期间轮流运行
 * - taskYIELD() 主动把 CPU 让给同优先级任务
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
    /* PA1/PA2 观察同优先级任务，PC13 观察高优先级任务。 */
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

static void led_toggle_pa2(void)
{
    if ((GPIOA->ODR & GPIO_ODR_ODR2) != 0U) {
        GPIOA->BRR = GPIO_BRR_BR2;
    } else {
        GPIOA->BSRR = GPIO_BSRR_BS2;
    }
}

void vApplicationMallocFailedHook(void)
{
    /* 三个任务任意一个创建失败，都可能导致对应 PA1/PA2/PC13 现象缺失。 */
    taskDISABLE_INTERRUPTS();
    while (1) {
    }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    /* 本课任务不阻塞的 same_a/same_b 会频繁运行，栈问题应停在这里排查。 */
    (void)task;
    (void)task_name;

    taskDISABLE_INTERRUPTS();
    while (1) {
    }
}

static void same_a(void *arg)
{
    (void)arg;

    while (1) {
        led_toggle_pa1();

        /*
         * same_a 不延时，所以它几乎一直就绪。
         * taskYIELD() 让同优先级的 same_b 有机会马上运行。
         */
        taskYIELD();
    }
}

static void same_b(void *arg)
{
    (void)arg;

    while (1) {
        led_toggle_pa2();
        taskYIELD();
    }
}

static void high_task(void *arg)
{
    (void)arg;

    while (1) {
        led_toggle_pc13();

        /*
         * high_task 优先级最高。
         * 如果它不阻塞，PA1/PA2 两个低优先级任务几乎没有运行机会。
         */
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

int main(void)
{
    system_clock_72mhz_init();
    gpio_init();

    /*
     * same_a 和 same_b 同为优先级 1，专门用于观察同优先级调度。
     * high_task 优先级 2，醒来时会抢占它们。
     */
    BaseType_t a_ok = xTaskCreate(same_a, "a", 128, NULL, 1, NULL);
    BaseType_t b_ok = xTaskCreate(same_b, "b", 128, NULL, 1, NULL);
    BaseType_t high_ok = xTaskCreate(high_task, "high", 128, NULL, 2, NULL);

    if ((a_ok != pdPASS) || (b_ok != pdPASS) || (high_ok != pdPASS)) {
        taskDISABLE_INTERRUPTS();
        while (1) {
        }
    }

    vTaskStartScheduler();

    while (1) {
    }
}

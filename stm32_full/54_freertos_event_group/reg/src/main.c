#include "stm32f1xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"

/*
 * 寄存器版：FreeRTOS 事件组。
 *
 * 53 的队列集等待“多个对象中哪个可读”。
 * 本课的事件组等待“同一个 bit 集合中哪些条件满足”。
 *
 * BIT_A 由 task_a 设置，BIT_B 由 task_b 设置；
 * wait_task 必须等 A 和 B 都到齐，才翻转 PC13。
 */

#define BIT_A (1U << 0)
#define BIT_B (1U << 1)

static EventGroupHandle_t g_events;

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
    /* PA1/PA2 显示 A/B 条件被设置，PC13 显示两个条件都到齐。 */
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

static void stop_for_debug(void)
{
    taskDISABLE_INTERRUPTS();
    while (1) {
    }
}

void vApplicationMallocFailedHook(void)
{
    /* 事件组对象和任务栈都来自 FreeRTOS heap，分配失败停在这里。 */
    stop_for_debug();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;
    stop_for_debug();
}

static void task_a(void *arg)
{
    (void)arg;

    while (1) {
        /*
         * 设置 BIT_A 表示条件 A 已经发生。
         * 事件组 bit 是状态位，不是队列；多次 set 在清除前仍只是 bit=1。
         */
        xEventGroupSetBits(g_events, BIT_A);
        led_toggle_pa1();

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void task_b(void *arg)
{
    (void)arg;

    while (1) {
        xEventGroupSetBits(g_events, BIT_B);
        led_toggle_pa2();

        vTaskDelay(pdMS_TO_TICKS(900));
    }
}

static void wait_task(void *arg)
{
    (void)arg;

    while (1) {
        /*
         * 第三个参数 pdTRUE：返回时清除 BIT_A 和 BIT_B。
         * 第四个参数 pdTRUE：必须等两个 bit 都满足，而不是任意一个满足。
         */
        (void)xEventGroupWaitBits(g_events,
                                  BIT_A | BIT_B,
                                  pdTRUE,
                                  pdTRUE,
                                  portMAX_DELAY);

        led_toggle_pc13();
    }
}

int main(void)
{
    system_clock_72mhz_init();
    gpio_init();

    g_events = xEventGroupCreate();

    BaseType_t a_ok = xTaskCreate(task_a, "a", 128, NULL, 1, NULL);
    BaseType_t b_ok = xTaskCreate(task_b, "b", 128, NULL, 1, NULL);
    BaseType_t wait_ok = xTaskCreate(wait_task, "wait", 160, NULL, 2, NULL);

    if ((g_events == NULL) ||
        (a_ok != pdPASS) ||
        (b_ok != pdPASS) ||
        (wait_ok != pdPASS)) {
        stop_for_debug();
    }

    vTaskStartScheduler();

    stop_for_debug();
}

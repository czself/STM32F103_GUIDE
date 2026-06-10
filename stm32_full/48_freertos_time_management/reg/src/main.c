#include "stm32f1xx.h"
#include "FreeRTOS.h"
#include "task.h"

/*
 * 寄存器版：FreeRTOS 时间管理。
 *
 * 47 已经讲过调度器“选谁运行”。本课讲任务“什么时候重新就绪”：
 * - vTaskDelay() 是从当前时刻开始的相对延时
 * - vTaskDelayUntil() 是按上一次唤醒点推算下一次固定周期
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
    /* PC13 显示固定周期任务，PA1 显示相对延时任务。 */
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

void vApplicationMallocFailedHook(void)
{
    /* 任务创建失败时会停在这里；这不是延时 API 的时间计算问题。 */
    taskDISABLE_INTERRUPTS();
    while (1) {
    }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    /* 如果任务栈不足，LED 可能停住；先排除 hook，再分析周期漂移。 */
    (void)task;
    (void)task_name;

    taskDISABLE_INTERRUPTS();
    while (1) {
    }
}

static void precise_task(void *arg)
{
    (void)arg;

    /*
     * last 是固定周期的参考点，所以放在循环外。
     * vTaskDelayUntil() 每次都会用它推算下一次唤醒点，并更新 last。
     */
    TickType_t last = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(500);

    while (1) {
        led_toggle_pc13();
        vTaskDelayUntil(&last, period);
    }
}

static void relative_task(void *arg)
{
    (void)arg;

    while (1) {
        led_toggle_pa1();

        /*
         * vTaskDelay() 从“执行到这里这一刻”再等待 700ms。
         * 如果任务本身执行变慢，下一次翻转时间也会跟着后移。
         */
        vTaskDelay(pdMS_TO_TICKS(700));
    }
}

int main(void)
{
    system_clock_72mhz_init();
    gpio_init();

    /*
     * precise 优先级更高，用来减少固定周期任务的唤醒延迟。
     * relative 优先级较低，但 precise 很快阻塞，所以它仍会运行。
     */
    BaseType_t precise_ok = xTaskCreate(precise_task, "until", 128, NULL, 2, NULL);
    BaseType_t relative_ok = xTaskCreate(relative_task, "delay", 128, NULL, 1, NULL);

    if ((precise_ok != pdPASS) || (relative_ok != pdPASS)) {
        taskDISABLE_INTERRUPTS();
        while (1) {
        }
    }

    vTaskStartScheduler();

    while (1) {
    }
}

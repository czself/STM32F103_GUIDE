#include "stm32f1xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/*
 * 寄存器版：互斥量与优先级反转。
 *
 * 51 已经讲过二值/计数信号量。本课的新重点是互斥量：
 * - 互斥量保护“同一份共享资源”
 * - 拿到互斥量的任务拥有所有权，用完必须 give
 * - 高优先级任务等待低优先级持有者时，互斥量可触发优先级继承
 *
 * GPIO 只是观察点：PA1=low 进入资源区，PA2=mid 周期运行，PC13=high 拿到资源。
 */

static SemaphoreHandle_t g_mutex;

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
    /* 互斥量对象和任务栈都来自 FreeRTOS heap；分配失败停在这里。 */
    stop_for_debug();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;
    stop_for_debug();
}

static void low_task(void *arg)
{
    (void)arg;

    while (1) {
        if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
            /*
             * low 成为互斥量所有者，等价于进入共享资源区。
             * PA1 翻转表示“低优先级任务已经持有资源”。
             */
            led_toggle_pa1();

            /*
             * 这里故意持锁延时 600ms，用来放大优先级反转现象。
             * 真实工程中持锁区应尽量短，不能长时间阻塞。
             */
            vTaskDelay(pdMS_TO_TICKS(600));

            /* 释放互斥量后，若发生过优先级继承，low 会恢复原优先级。 */
            xSemaphoreGive(g_mutex);
        }

        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

static void mid_task(void *arg)
{
    (void)arg;

    while (1) {
        /*
         * mid 不拿互斥量，只模拟中优先级任务持续运行。
         * 它是观察优先级反转背景的干扰任务。
         */
        led_toggle_pa2();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void high_task(void *arg)
{
    (void)arg;

    while (1) {
        /*
         * 先延时 200ms，让 low 有机会先拿到互斥量。
         * 否则 high 优先级最高，可能先拿到资源，就构造不出反转场景。
         */
        vTaskDelay(pdMS_TO_TICKS(200));

        if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
            led_toggle_pc13();
            xSemaphoreGive(g_mutex);
        }
    }
}

int main(void)
{
    system_clock_72mhz_init();
    gpio_init();

    g_mutex = xSemaphoreCreateMutex();

    BaseType_t low_ok = xTaskCreate(low_task, "low", 128, NULL, 1, NULL);
    BaseType_t mid_ok = xTaskCreate(mid_task, "mid", 128, NULL, 2, NULL);
    BaseType_t high_ok = xTaskCreate(high_task, "high", 128, NULL, 3, NULL);

    if ((g_mutex == NULL) ||
        (low_ok != pdPASS) ||
        (mid_ok != pdPASS) ||
        (high_ok != pdPASS)) {
        stop_for_debug();
    }

    vTaskStartScheduler();

    stop_for_debug();
}

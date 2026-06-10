#include "stm32f1xx.h"
#include "FreeRTOS.h"
#include "task.h"

/*
 * 寄存器版：FreeRTOS 任务通知。
 *
 * 54 的事件组是独立内核对象；本课的任务通知没有额外对象。
 * 通知状态和值存在目标任务自己的 TCB 中，所以 sender 必须拿到 led_task 句柄。
 */

static TaskHandle_t g_led_task;

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
    /* PC13 是 led_task 收到通知后的现象输出。 */
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
    /* 通知本身不创建对象，但任务 TCB/栈仍然来自 FreeRTOS heap。 */
    stop_for_debug();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;
    stop_for_debug();
}

static void sender_task(void *arg)
{
    (void)arg;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(500));

        /*
         * 通知目标是 led_task 这个任务对象，不是 led_task 函数名。
         * xTaskNotifyGive() 会让目标任务 TCB 中的通知值加 1。
         */
        xTaskNotifyGive(g_led_task);
    }
}

static void led_task(void *arg)
{
    (void)arg;

    while (1) {
        /*
         * pdTRUE 表示收到通知后把通知值清零。
         * 没有通知时 led_task 阻塞，不占 CPU。
         */
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        led_toggle_pc13();
    }
}

int main(void)
{
    system_clock_72mhz_init();
    gpio_init();

    /*
     * 保存 led_task 句柄是本课关键。
     * sender 后面必须靠 g_led_task 指定“通知哪个任务”。
     */
    BaseType_t led_ok = xTaskCreate(led_task, "led", 128, NULL, 2, &g_led_task);
    BaseType_t sender_ok = xTaskCreate(sender_task, "send", 128, NULL, 1, NULL);

    if ((led_ok != pdPASS) || (sender_ok != pdPASS)) {
        stop_for_debug();
    }

    vTaskStartScheduler();

    stop_for_debug();
}

#include "stm32f1xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/*
 * 寄存器版：FreeRTOS 队列。
 *
 * 49 已经用队列把 ISR 事件交给任务。本课去掉中断，只看两个普通任务：
 * producer 负责产生 1 字节事件，consumer 阻塞等待队列并处理事件。
 *
 * 队列是 RTOS 内核对象，不是 GPIO 外设。PC13 只用来显示 consumer 收到消息。
 */

static QueueHandle_t g_queue;

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
    /* PC13 是 consumer 收到队列消息后的现象输出；PA0/PA1/PA2 当前不参与队列逻辑。 */
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
    /* 队列控制块、队列存储区、任务 TCB/栈都来自 FreeRTOS heap。 */
    stop_for_debug();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;
    stop_for_debug();
}

static void producer(void *arg)
{
    const uint8_t event_value = 1U;

    (void)arg;

    while (1) {
        /*
         * xQueueSend() 会把 event_value 的 1 字节内容复制进队列。
         * 第三个参数为 0，表示队列满时不等待，立即返回失败。
         */
        (void)xQueueSend(g_queue, &event_value, 0);

        vTaskDelay(pdMS_TO_TICKS(700));
    }
}

static void consumer(void *arg)
{
    uint8_t event_value;

    (void)arg;

    while (1) {
        /*
         * 队列为空时，consumer 阻塞在这里，不占 CPU。
         * 收到消息后才翻转 PC13，所以 PC13 的节奏由 producer 发送节奏决定。
         */
        if (xQueueReceive(g_queue, &event_value, portMAX_DELAY) == pdPASS) {
            led_toggle_pc13();
        }
    }
}

int main(void)
{
    system_clock_72mhz_init();
    gpio_init();

    /*
     * 长度 4 表示最多缓存 4 个未处理事件。
     * 元素大小 sizeof(uint8_t) 表示每条消息复制 1 字节。
     */
    g_queue = xQueueCreate(4, sizeof(uint8_t));

    BaseType_t producer_ok = xTaskCreate(producer, "prod", 128, NULL, 2, NULL);
    BaseType_t consumer_ok = xTaskCreate(consumer, "cons", 128, NULL, 1, NULL);

    if ((g_queue == NULL) || (producer_ok != pdPASS) || (consumer_ok != pdPASS)) {
        stop_for_debug();
    }

    vTaskStartScheduler();

    stop_for_debug();
}

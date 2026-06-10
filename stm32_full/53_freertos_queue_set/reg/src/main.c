#include "stm32f1xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/*
 * 寄存器版：FreeRTOS 队列集。
 *
 * 50 已经讲过单个队列。本课的新问题是：
 * 一个任务如果要同时等多个队列，不能靠手写轮询到处试探。
 *
 * 队列集负责告诉 selector：“哪个成员现在可读”。
 * 真正的数据仍然在原队列里，所以选中后还要回到 g_a 或 g_b 接收。
 */

static QueueHandle_t g_a;
static QueueHandle_t g_b;
static QueueSetHandle_t g_set;

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
    /* PC13 显示 A 来源，PA1 显示 B 来源。GPIO 只是 selector 处理结果的现象。 */
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
    /* 队列、队列集、任务 TCB 和栈都可能消耗 FreeRTOS heap。 */
    stop_for_debug();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;
    stop_for_debug();
}

static void prod_a(void *arg)
{
    const uint8_t value = 1U;

    (void)arg;

    while (1) {
        /*
         * producer A 只往 g_a 发送，不直接翻转 PC13。
         * 这样才能体现 selector 统一等待多个来源的作用。
         */
        (void)xQueueSend(g_a, &value, 0);
        vTaskDelay(pdMS_TO_TICKS(700));
    }
}

static void prod_b(void *arg)
{
    const uint8_t value = 2U;

    (void)arg;

    while (1) {
        (void)xQueueSend(g_b, &value, 0);
        vTaskDelay(pdMS_TO_TICKS(1100));
    }
}

static void select_task(void *arg)
{
    QueueSetMemberHandle_t active;
    uint8_t value;

    (void)arg;

    while (1) {
        /*
         * 队列集阻塞等待任意成员可读。
         * 返回值是“哪个对象可读”，不是队列里的 uint8_t 数据。
         */
        active = xQueueSelectFromSet(g_set, portMAX_DELAY);

        if (active == g_a) {
            /*
             * 选中 g_a 后，真正的数据还在 g_a 里。
             * 这里用 0 等待，因为队列集已经保证它可读。
             */
            if (xQueueReceive(g_a, &value, 0) == pdPASS) {
                led_toggle_pc13();
            }
        } else if (active == g_b) {
            if (xQueueReceive(g_b, &value, 0) == pdPASS) {
                led_toggle_pa1();
            }
        } else {
            /*
             * 当前只有两个成员。若后续添加更多成员却忘记处理，
             * 默认分支能让错误停在明确位置。
             */
            stop_for_debug();
        }
    }
}

int main(void)
{
    system_clock_72mhz_init();
    gpio_init();

    g_a = xQueueCreate(4, sizeof(uint8_t));
    g_b = xQueueCreate(4, sizeof(uint8_t));

    /*
     * 队列集容量需要覆盖成员队列可能积压的事件数。
     * g_a 长度 4，g_b 长度 4，所以容量用 8。
     */
    g_set = xQueueCreateSet(8);

    if ((g_a == NULL) || (g_b == NULL) || (g_set == NULL)) {
        stop_for_debug();
    }

    if (xQueueAddToSet(g_a, g_set) != pdPASS) {
        stop_for_debug();
    }

    if (xQueueAddToSet(g_b, g_set) != pdPASS) {
        stop_for_debug();
    }

    BaseType_t a_ok = xTaskCreate(prod_a, "pa", 128, NULL, 1, NULL);
    BaseType_t b_ok = xTaskCreate(prod_b, "pb", 128, NULL, 1, NULL);
    BaseType_t sel_ok = xTaskCreate(select_task, "sel", 160, NULL, 2, NULL);

    if ((a_ok != pdPASS) || (b_ok != pdPASS) || (sel_ok != pdPASS)) {
        stop_for_debug();
    }

    vTaskStartScheduler();

    stop_for_debug();
}

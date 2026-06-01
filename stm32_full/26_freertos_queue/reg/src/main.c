#include "stm32f1xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/*
 * ============================================================================
 * 寄存器版 FreeRTOS 队列实验
 * ============================================================================
 *
 * ██████  本课核心知识点 ██████
 *
 * 1. 队列（Queue）—— 任务间通信的核心机制
 *    - 队列 = 先进先出（FIFO）缓冲区
 *    - 发送方（key_task）：往队列里放事件
 *    - 接收方（led_task）：从队列里取事件
 *    - 队列解决了任务间"不直接访问共享变量"的问题
 *
 * 2. 阻塞等待（Blocking Receive）
 *    - xQueueReceive(queue, &data, portMAX_DELAY)
 *    - 如果队列为空，任务进入阻塞态，不消耗 CPU
 *    - 当有新数据进入队列时，任务自动被唤醒
 *    - 这就是"事件驱动"的核心思想
 *
 * 3. xQueueCreate() —— 创建队列
 *    - 参数：队列长度（元素个数）和每个元素的大小（字节数）
 *    - 返回队列句柄，后续操作都通过这个句柄
 *    - 队列在 FreeRTOS 的 heap 中分配内存
 *
 * 4. xQueueSend() —— 队列发送
 *    - 第三个参数 = 0：不等待，队列满时立即返回
 *    - 还有 portMAX_DELAY 选项（队列满时阻塞等待）
 *
 * 5. pdMS_TO_TICKS() —— 毫秒转 Tick
 *    - FreeRTOS 内部用 Tick 计数（configTICK_RATE_HZ = 1000 → 1ms = 1 Tick）
 *    - pdMS_TO_TICKS(20) = 20 个 Tick = 20ms
 *
 * 6. 任务优先级的作用
 *    - key_task 优先级 2（高）：按键检测需要及时响应
 *    - led_task 优先级 1（低）：LED 操作不紧急
 *    - 如果两个就绪任务优先级相同，FreeRTOS 用时间片轮流调度
 *    - 不同优先级时，高优先级永远先运行
 */

static QueueHandle_t g_key_queue;

static void system_clock_72mhz_init(void)
{
    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;
    RCC->CR |= RCC_CR_HSEON;
    while ((RCC->CR & RCC_CR_HSERDY) == 0U) {}
    RCC->CFGR &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2 |
                   RCC_CFGR_PLLSRC | RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMULL |
                   RCC_CFGR_SW);
    RCC->CFGR |= RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 |
                 RCC_CFGR_PPRE2_DIV1 | RCC_CFGR_PLLSRC | RCC_CFGR_PLLMULL9;
    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0U) {}
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {}
}

static void gpio_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN | RCC_APB2ENR_IOPAEN;

    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
    GPIOC->CRH |= GPIO_CRH_MODE13_1;
    GPIOC->BSRR = GPIO_BSRR_BS13;

    GPIOA->CRL &= ~(GPIO_CRL_MODE0 | GPIO_CRL_CNF0);
    GPIOA->CRL |= GPIO_CRL_CNF0_1;
    GPIOA->BSRR = GPIO_BSRR_BS0;
}

static uint8_t key_pressed(void)
{
    return (GPIOA->IDR & GPIO_IDR_IDR0) == 0U;
}

static void led_toggle(void)
{
    if ((GPIOC->ODR & GPIO_ODR_ODR13) != 0U) GPIOC->BRR = GPIO_BRR_BR13;
    else GPIOC->BSRR = GPIO_BSRR_BS13;
}

static void key_task(void *argument)
{
    uint8_t last = 0U;
    (void)argument;

    while (1) {
        uint8_t now = key_pressed();
        if ((now != 0U) && (last == 0U)) {
            uint8_t event = 1U;
            xQueueSend(g_key_queue, &event, 0);
        }
        last = now;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void led_task(void *argument)
{
    uint8_t event;
    (void)argument;

    while (1) {
        if (xQueueReceive(g_key_queue, &event, portMAX_DELAY) == pdPASS) {
            (void)event;
            led_toggle();
        }
    }
}

void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    while (1) {}
}

int main(void)
{
    system_clock_72mhz_init();
    gpio_init();

    g_key_queue = xQueueCreate(4, sizeof(uint8_t));
    xTaskCreate(key_task, "key", 128, NULL, 2, NULL);
    xTaskCreate(led_task, "led", 128, NULL, 1, NULL);
    vTaskStartScheduler();

    while (1) {}
}

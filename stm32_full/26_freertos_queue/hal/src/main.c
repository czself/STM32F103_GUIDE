#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/*
 * ============================================================================
 * HAL 版 FreeRTOS 队列实验
 * ============================================================================
 *
 * 队列解耦了按键任务和 LED 任务的职责：
 * - key_task：只负责读 GPIO、检测按键、发送事件
 * - led_task：只负责等待事件、翻转 LED
 *
 * 关键设计思想：
 *   按键任务不知道 LED 的存在，LED 任务不知道按键的存在。
 *   两者只通过队列交换事件。这是 RTOS 程序设计的基本范式。
 *
 * HAL 版和寄存器版的区别仅在于 GPIO 读取方式：
 *   寄存器版：(GPIOA->IDR & GPIO_IDR_IDR0) == 0U
 *   HAL 版：  HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET
 *   队列 API 完全相同，因为队列属于 FreeRTOS，和 HAL 无关。
 */

static QueueHandle_t g_key_queue;

static void system_clock_72mhz_init(void);
static void gpio_init(void);

static void key_task(void *argument)
{
    uint8_t last = 0U;
    (void)argument;

    while (1) {
        uint8_t now = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET);
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
            HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        }
    }
}

int main(void)
{
    HAL_Init();
    system_clock_72mhz_init();
    gpio_init();

    g_key_queue = xQueueCreate(4, sizeof(uint8_t));
    xTaskCreate(key_task, "key", 128, NULL, 2, NULL);
    xTaskCreate(led_task, "led", 128, NULL, 1, NULL);
    vTaskStartScheduler();

    while (1) {}
}

static void gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &gpio);
}

static void system_clock_72mhz_init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL9;
    HAL_RCC_OscConfig(&osc);
    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2);
}

void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    while (1) {}
}

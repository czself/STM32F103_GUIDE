#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/*
 * HAL 版：FreeRTOS 队列。
 *
 * HAL 只负责 PC13 的 GPIO 输出；队列对象、阻塞等待和任务唤醒都由 FreeRTOS 管。
 */

static QueueHandle_t g_queue;

static void system_clock_72mhz_init(void);
static void gpio_init(void);
static void stop_for_debug(void);

static void system_clock_72mhz_init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    /* 72MHz 时钟要和 FreeRTOSConfig.h 保持一致，否则任务延时节奏会偏。 */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        stop_for_debug();
    }

    clk.ClockType = RCC_CLOCKTYPE_SYSCLK |
                    RCC_CLOCKTYPE_HCLK |
                    RCC_CLOCKTYPE_PCLK1 |
                    RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) {
        stop_for_debug();
    }
}

static void gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* PC13 对应 HAL_GPIO_Init() 写 GPIOC 模式寄存器，作为 consumer 现象输出。 */
    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

    gpio.Pin = GPIO_PIN_1 | GPIO_PIN_2;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &gpio);
}

static void stop_for_debug(void)
{
    taskDISABLE_INTERRUPTS();
    while (1) {
    }
}

void vApplicationMallocFailedHook(void)
{
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
        (void)xQueueSend(g_queue, &event_value, 0);
        vTaskDelay(pdMS_TO_TICKS(700));
    }
}

static void consumer(void *arg)
{
    uint8_t event_value;

    (void)arg;

    while (1) {
        if (xQueueReceive(g_queue, &event_value, portMAX_DELAY) == pdPASS) {
            HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        }
    }
}

int main(void)
{
    HAL_Init();
    system_clock_72mhz_init();
    gpio_init();

    g_queue = xQueueCreate(4, sizeof(uint8_t));

    BaseType_t producer_ok = xTaskCreate(producer, "prod", 128, NULL, 2, NULL);
    BaseType_t consumer_ok = xTaskCreate(consumer, "cons", 128, NULL, 1, NULL);

    if ((g_queue == NULL) || (producer_ok != pdPASS) || (consumer_ok != pdPASS)) {
        stop_for_debug();
    }

    vTaskStartScheduler();

    stop_for_debug();
}

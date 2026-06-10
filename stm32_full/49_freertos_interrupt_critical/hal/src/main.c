#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/*
 * HAL 版：FreeRTOS 中断与临界区。
 *
 * HAL 负责把 PA0 配成 EXTI 输入，并把 IRQHandler 分发到 callback。
 * 但 callback 仍然在中断上下文里，所以仍必须使用 xQueueSendFromISR()。
 */

static QueueHandle_t g_queue;
static volatile uint32_t g_shared;

static void system_clock_72mhz_init(void);
static void gpio_exti_init(void);
static void stop_for_debug(void);

static void system_clock_72mhz_init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    /* HSE + PLL x9 对齐 FreeRTOSConfig.h 的 72MHz CPU 时钟。 */
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

static void gpio_exti_init(void)
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

    gpio.Pin = GPIO_PIN_1 | GPIO_PIN_2;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio);

    /*
     * PA0 配成下降沿 EXTI 输入并启用上拉。
     * 这对应寄存器版的 GPIO 输入上拉、AFIO EXTI0 映射、FTSR 下降沿。
     */
    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_IT_FALLING;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &gpio);

    HAL_NVIC_SetPriority(EXTI0_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(EXTI0_IRQn);
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

static void event_task(void *arg)
{
    uint8_t event_value;

    (void)arg;

    while (1) {
        if (xQueueReceive(g_queue, &event_value, portMAX_DELAY) == pdPASS) {
            taskENTER_CRITICAL();
            g_shared++;
            taskEXIT_CRITICAL();

            HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        }
    }
}

void EXTI0_IRQHandler(void)
{
    /*
     * HAL 会在这里检查并清 EXTI pending，然后调用 HAL_GPIO_EXTI_Callback()。
     * 业务仍不要直接写在 IRQHandler 里。
     */
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0);
}

void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
    BaseType_t higher_priority_woken = pdFALSE;

    if (pin == GPIO_PIN_0) {
        uint8_t event_value = 1U;

        (void)xQueueSendFromISR(g_queue, &event_value, &higher_priority_woken);
    }

    portYIELD_FROM_ISR(higher_priority_woken);
}

int main(void)
{
    HAL_Init();
    system_clock_72mhz_init();
    gpio_exti_init();

    g_queue = xQueueCreate(4, sizeof(uint8_t));
    BaseType_t task_ok = xTaskCreate(event_task, "evt", 160, NULL, 2, NULL);

    if ((g_queue == NULL) || (task_ok != pdPASS)) {
        stop_for_debug();
    }

    vTaskStartScheduler();

    stop_for_debug();
}

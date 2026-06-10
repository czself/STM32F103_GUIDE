#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

/*
 * HAL 版：FreeRTOS 时间管理。
 *
 * HAL 版也不在任务里用 HAL_Delay()。在 RTOS 任务中，等待时间应交给
 * vTaskDelay() / vTaskDelayUntil()，这样任务会阻塞并让出 CPU。
 */

static void system_clock_72mhz_init(void);
static void gpio_init(void);
static void error_stop(void);

static void system_clock_72mhz_init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    /* tick 时间准确性来自真实系统时钟；这里必须与 FreeRTOSConfig.h 的 72MHz 对齐。 */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        error_stop();
    }

    /* HAL_RCC_ClockConfig() 会把这些字段写入 RCC/Flash 相关配置。 */
    clk.ClockType = RCC_CLOCKTYPE_SYSCLK |
                    RCC_CLOCKTYPE_HCLK |
                    RCC_CLOCKTYPE_PCLK1 |
                    RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) {
        error_stop();
    }
}

static void gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* PC13 显示 vTaskDelayUntil() 管理的固定周期任务。 */
    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

    /* PA1 显示 vTaskDelay() 管理的相对延时任务；PA2 当前不用。 */
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

void vApplicationMallocFailedHook(void)
{
    /* 任务没创建成功时，周期现象不存在；先看 hook，再看延时参数。 */
    taskDISABLE_INTERRUPTS();
    while (1) {
    }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    /* 栈溢出会导致系统停住，不是 vTaskDelayUntil() 的周期漂移。 */
    (void)task;
    (void)task_name;

    taskDISABLE_INTERRUPTS();
    while (1) {
    }
}

static void precise_task(void *arg)
{
    (void)arg;

    TickType_t last = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(500);

    while (1) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);

        /* 使用固定周期唤醒点，减少任务执行时间带来的周期漂移。 */
        vTaskDelayUntil(&last, period);
    }
}

static void relative_task(void *arg)
{
    (void)arg;

    while (1) {
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_1);
        /*
         * 相对延时从“当前执行到这里”开始计算。
         * 这和 precise_task 的固定唤醒点不同。
         */
        vTaskDelay(pdMS_TO_TICKS(700));
    }
}

int main(void)
{
    HAL_Init();
    system_clock_72mhz_init();
    gpio_init();

    /*
     * 两个任务故意用不同时间模型：
     * precise 观察固定周期，relative 观察相对等待。
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

static void error_stop(void)
{
    __disable_irq();
    while (1) {
    }
}

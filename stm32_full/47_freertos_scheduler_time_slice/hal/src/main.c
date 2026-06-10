#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

/*
 * HAL 版：调度器与时间片。
 *
 * HAL_GPIO_TogglePin() 只是现象输出。本课真正要看的是
 * 优先级、阻塞和 taskYIELD() 怎样影响三个任务的运行顺序。
 */

static void system_clock_72mhz_init(void);
static void gpio_init(void);
static void error_stop(void);

static void system_clock_72mhz_init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    /* 调度时间片来自 SysTick；SysTick 的节奏建立在真实 72MHz 时钟上。 */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        error_stop();
    }

    /* 这些 HAL 字段会落到 RCC CFGR 的系统时钟来源和总线分频配置。 */
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

    /* PC13 代表 high_task，不代表调度器本身。 */
    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

    /* PA1/PA2 分别代表两个同优先级任务，适合用逻辑分析仪观察轮转。 */
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
    /* 若某个任务创建失败，对应引脚可能完全没有变化。 */
    taskDISABLE_INTERRUPTS();
    while (1) {
    }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    /* 栈溢出会让系统停住，和时间片是否公平不是同一类问题。 */
    (void)task;
    (void)task_name;

    taskDISABLE_INTERRUPTS();
    while (1) {
    }
}

static void same_a(void *arg)
{
    (void)arg;

    while (1) {
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_1);
        /*
         * 主动让出 CPU，给同优先级 same_b 一个立即运行机会。
         * 这不是延时，same_a 仍然保持就绪态。
         */
        taskYIELD();
    }
}

static void same_b(void *arg)
{
    (void)arg;

    while (1) {
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_2);
        taskYIELD();
    }
}

static void high_task(void *arg)
{
    (void)arg;

    while (1) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);

        /* 高优先级任务主动阻塞后，同优先级的 PA1/PA2 任务才有 CPU。 */
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

int main(void)
{
    HAL_Init();
    system_clock_72mhz_init();
    gpio_init();

    /*
     * 同优先级任务用相同优先级 1；high_task 用 2。
     * 这样才能观察“高优先级优先，同优先级轮转”的调度规则。
     */
    BaseType_t a_ok = xTaskCreate(same_a, "a", 128, NULL, 1, NULL);
    BaseType_t b_ok = xTaskCreate(same_b, "b", 128, NULL, 1, NULL);
    BaseType_t high_ok = xTaskCreate(high_task, "high", 128, NULL, 2, NULL);

    if ((a_ok != pdPASS) || (b_ok != pdPASS) || (high_ok != pdPASS)) {
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

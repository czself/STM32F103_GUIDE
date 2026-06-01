#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

/*
 * ============================================================================
 * HAL 版 FreeRTOS 基础：多任务调度
 * ============================================================================
 *
 * 本文件演示 HAL 库 + FreeRTOS 的基本结构：
 * - HAL 负责硬件初始化（时钟、GPIO）
 * - FreeRTOS 负责多任务调度
 *
 * 两个任务：
 *   led_task    (优先级 2)：每 500ms 翻转 PC13（板载 LED）
 *   heartbeat_task (优先级 1)：每 1000ms 翻转 PA1（可外接 LED/示波器）
 *
 * 为什么 FreeRTOS 中不能用 HAL_Delay()？
 *   HAL_Delay() 依赖 SysTick 中断，而 FreeRTOS 也使用 SysTick。
 *   如果在任务中使用 HAL_Delay()，可能会和 FreeRTOS 的 Tick 机制冲突。
 *   应该统一使用 vTaskDelay() 或 xTaskDelayUntil()。
 *
 * 为什么这里还需要 SysTick_Handler？
 *   FreeRTOSConfig.h 中定义了 xPortSysTickHandler → SysTick_Handler，
 *   SysTick_Handler 内部调用 HAL_IncTick() + xPortSysTickHandler，
 *   前者为 HAL_Delay() 提供时钟，后者为 FreeRTOS 提供 Tick。
 */

static void system_clock_72mhz_init(void);
static void gpio_init(void);

static void led_task(void *argument)
{
    (void)argument;
    while (1) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void heartbeat_task(void *argument)
{
    (void)argument;
    while (1) {
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_1);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

int main(void)
{
    HAL_Init();
    system_clock_72mhz_init();
    gpio_init();

    xTaskCreate(led_task, "led", 128, NULL, 2, NULL);
    xTaskCreate(heartbeat_task, "beat", 128, NULL, 1, NULL);

    vTaskStartScheduler();

    while (1) {
    }
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

    gpio.Pin = GPIO_PIN_1;
    HAL_GPIO_Init(GPIOA, &gpio);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_RESET);
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
    while (1) {
    }
}

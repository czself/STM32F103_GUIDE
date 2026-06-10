#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

/*
 * HAL版：FreeRTOS Tickless Idle。
 *
 * HAL 负责 RCC/GPIO 封装；tickless idle 仍由本课本地 FreeRTOSConfig.h
 * 打开。vApplicationSleep() 里只执行 __WFI()，不要把这节课误读成
 * Stop/Standby 低功耗工程。
 */

static void stop_for_debug(void)
{
    taskDISABLE_INTERRUPTS();
    while (1) {
    }
}

static void system_clock_72mhz_init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    /* 仍然配置 72MHz；本课不是通过降频讲低功耗。 */
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

    /* PC13 是 slow_task 被唤醒后继续执行的可见证据。 */
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

void vApplicationMallocFailedHook(void)
{
    /*
     * tickless 不取消 FreeRTOS 对 heap 的需求。
     * idle task 和 slow_task 创建失败时，系统没有机会进入正常调度。
     */
    stop_for_debug();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;
    stop_for_debug();
}

void vApplicationSleep(TickType_t expected_idle_time)
{
    /*
     * expected_idle_time 的单位是 tick，表示内核预计还能空闲多久。
     * 产品代码可能据此决定是否进入更深低功耗；本课只演示 tickless
     * 调用睡眠入口这一层，不配置 PWR/Stop/Standby。
     */
    (void)expected_idle_time;

    /*
     * __WFI() 属于 Cortex-M 内核指令层，HAL 没有替我们隐藏这一步。
     * 有中断到来后 CPU 才会继续执行。
     */
    __WFI();
}

static void slow_task(void *arg)
{
    (void)arg;

    while (1) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);

        /*
         * slow_task 阻塞 2000ms 后，系统大部分时间只剩 idle task。
         * 这正是 tickless idle 有机会介入的条件。
         */
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

int main(void)
{
    HAL_Init();
    system_clock_72mhz_init();
    gpio_init();

    BaseType_t slow_ok = xTaskCreate(slow_task,
                                     "slow",
                                     128,
                                     NULL,
                                     1,
                                     NULL);

    if (slow_ok != pdPASS) {
        stop_for_debug();
    }

    vTaskStartScheduler();

    stop_for_debug();
}

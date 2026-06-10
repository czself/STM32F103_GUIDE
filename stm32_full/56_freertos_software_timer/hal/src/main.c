#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

/*
 * HAL版：FreeRTOS 软件定时器。
 *
 * HAL 只接管时钟和 GPIO 写法，软件定时器仍然是 FreeRTOS 内核对象。
 * PC13 由 timer callback 翻转，PA1 由普通任务翻转，用现象区分两条执行路径。
 */

static TimerHandle_t g_timer;

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

    /*
     * 这些字段对应寄存器版的 HSEON、PLLSRC、PLLMULL9。
     * 本课不展开 RCC 细节，只要知道 tick 和软件定时器周期都建立在
     * 正确系统时钟之上。
     */
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

    /* PC13 是软件定时器回调的现象输出。 */
    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

    /* PA1 是普通 monitor 任务的现象输出。 */
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
     * xTimerCreate() 和 xTaskCreate() 都会从 FreeRTOS heap 取内存。
     * 进入这里时，常见原因是 configTOTAL_HEAP_SIZE 不够。
     */
    stop_for_debug();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;

    /*
     * timer callback 运行在 timer service task 里，回调写重了也会消耗
     * 定时器服务任务栈，而不是消耗 main 的栈。
     */
    stop_for_debug();
}

static void timer_cb(TimerHandle_t timer)
{
    (void)timer;

    /*
     * 这里不是 STM32 TIM 中断，也不是 GPIO 中断。
     * 它由 FreeRTOS timer service task 调用，所以回调要短，避免堵住
     * 整个软件定时器服务。
     */
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
}

static void monitor_task(void *arg)
{
    (void)arg;

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

    /*
     * pdTRUE 是本课的关键：它让定时器到期后自动重新装载周期。
     * 如果改成 pdFALSE，PC13 通常只会翻转一次。
     */
    g_timer = xTimerCreate("blink",
                           pdMS_TO_TICKS(500),
                           pdTRUE,
                           NULL,
                           timer_cb);

    BaseType_t monitor_ok = xTaskCreate(monitor_task,
                                        "mon",
                                        128,
                                        NULL,
                                        1,
                                        NULL);

    if ((g_timer == NULL) || (monitor_ok != pdPASS)) {
        stop_for_debug();
    }

    /*
     * 启动软件定时器实际是向 timer command queue 发命令。
     * 这里不等待队列空间，失败就停住，避免“代码看似启动但 PC13 不闪”。
     */
    BaseType_t timer_start_ok = xTimerStart(g_timer, 0);

    if (timer_start_ok != pdPASS) {
        stop_for_debug();
    }

    vTaskStartScheduler();

    stop_for_debug();
}

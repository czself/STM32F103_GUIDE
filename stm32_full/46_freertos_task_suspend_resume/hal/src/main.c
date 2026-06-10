#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

/*
 * HAL 版：任务挂起与恢复。
 *
 * GPIO 翻转由 HAL 完成；任务状态变化由 FreeRTOS 完成。
 * 本课不要把 LED 暂停误认为 HAL 延时变长，真正原因是 blink 被挂起。
 */

static TaskHandle_t g_blink;

static void system_clock_72mhz_init(void);
static void gpio_init(void);
static void error_stop(void);

static void system_clock_72mhz_init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    /* HSE + PLL x9 对应 72MHz，必须和 FreeRTOSConfig.h 的 CPU_CLOCK_HZ 对齐。 */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        error_stop();
    }

    /* APB1 /2 是 F103 常规限制；FreeRTOS 本身不替你修正硬件时钟配置。 */
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

    /* PC13 是 blink 是否被调度到的可见证据。 */
    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

    /* PA1/PA2 不是本课控制对象，配置好但不参与挂起/恢复逻辑。 */
    gpio.Pin = GPIO_PIN_1 | GPIO_PIN_2;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* PA0 当前不读取，暂停/恢复完全由 control_task 调用 RTOS API 完成。 */
    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &gpio);
}

void vApplicationMallocFailedHook(void)
{
    /* 任务创建失败时会停在这里；这和 blink 被挂起后 LED 暂停是两种现象。 */
    taskDISABLE_INTERRUPTS();
    while (1) {
    }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    /* 栈溢出会导致系统停住，不会表现为规律的一秒暂停/一秒恢复。 */
    (void)task;
    (void)task_name;

    taskDISABLE_INTERRUPTS();
    while (1) {
    }
}

static void blink_task(void *arg)
{
    (void)arg;

    while (1) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void control_task(void *arg)
{
    (void)arg;

    while (1) {
        /* 挂起态不会靠 tick 到期自动解除，必须由 vTaskResume() 恢复。 */
        vTaskSuspend(g_blink);
        vTaskDelay(pdMS_TO_TICKS(1000));

        vTaskResume(g_blink);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

int main(void)
{
    HAL_Init();
    system_clock_72mhz_init();
    gpio_init();

    /*
     * 最后一个参数 &g_blink 很关键。
     * 没有这个句柄，control_task 就不知道要挂起/恢复哪个任务。
     */
    BaseType_t blink_ok = xTaskCreate(blink_task, "blink", 128, NULL, 1, &g_blink);
    BaseType_t ctrl_ok = xTaskCreate(control_task, "ctrl", 128, NULL, 2, NULL);

    if ((blink_ok != pdPASS) || (ctrl_ok != pdPASS)) {
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

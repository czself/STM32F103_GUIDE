#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

/*
 * HAL版：FreeRTOS 内存与任务栈水位。
 *
 * HAL 只封装 RCC/GPIO，内存概念仍来自 FreeRTOS：
 * heap 用来分配任务对象，stack 是每个任务自己的运行栈。
 */

static volatile UBaseType_t g_stack_watermark;

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

    /* 对应寄存器版打开 HSE、选择 HSE 进 PLL、9 倍频到 72MHz。 */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        stop_for_debug();
    }

    /* APB1 二分频是 F103 72MHz 常见配置，避免 APB1 超过允许频率。 */
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

    /* GPIO_MODE_OUTPUT_PP 对应普通推挽输出，PC13 用来显示任务仍在运行。 */
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
     * xTaskCreate() 需要从 FreeRTOS heap 分配 TCB 和任务栈。
     * 如果 heap 不足，任务根本不会进入就绪态，PC13 也不会周期翻转。
     */
    stop_for_debug();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;

    /*
     * 栈溢出说明某个任务自己的 stack 不够，和 HAL GPIO 配置无关。
     * 停在这里时应优先看 task_name、栈深度和局部变量大小。
     */
    stop_for_debug();
}

static void memory_task(void *arg)
{
    (void)arg;

    while (1) {
        /*
         * NULL 查询当前任务。返回值越小，说明 memory_task 历史上越接近
         * 栈底；单位通常是 stack word，不是字节。
         */
        g_stack_watermark = uxTaskGetStackHighWaterMark(NULL);

        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

int main(void)
{
    HAL_Init();
    system_clock_72mhz_init();
    gpio_init();

    /*
     * 这里的 160 是任务栈深度，不是 heap 总大小。
     * heap 总大小在 FreeRTOSConfig.h 里配置，二者是本课最容易混淆的边界。
     */
    BaseType_t memory_ok = xTaskCreate(memory_task,
                                       "mem",
                                       160,
                                       NULL,
                                       1,
                                       NULL);

    if (memory_ok != pdPASS) {
        stop_for_debug();
    }

    vTaskStartScheduler();

    stop_for_debug();
}

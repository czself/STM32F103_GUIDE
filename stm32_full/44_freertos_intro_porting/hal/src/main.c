#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

/*
 * HAL 版：FreeRTOS 入门移植与基础调度。
 *
 * HAL 只负责时钟和 GPIO 初始化；任务创建、延时和调度仍然由 FreeRTOS 完成。
 * 本课要看清 HAL 外设层和 RTOS 任务层不是同一件事。
 */

static void system_clock_72mhz_init(void);
static void gpio_init(void);
static void error_stop(void);

static void system_clock_72mhz_init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    /*
     * 这组字段对应寄存器版的 HSEON、PLLSRC、PLLMULL。
     * FreeRTOSConfig.h 里写的是 72MHz，所以真实系统时钟也必须配到 72MHz；
     * 否则 vTaskDelay(pdMS_TO_TICKS(...)) 的实际时间会整体偏快或偏慢。
     */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL9;

    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        error_stop();
    }

    /*
     * 这组字段对应寄存器版的 SYSCLK 来源和 AHB/APB 分频。
     * APB1 仍然分到 36MHz，避免超过 F103 的 APB1 频率上限。
     */
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

    /*
     * PC13 是 led_task 的现象输出。
     * HAL_GPIO_Init() 底层会写 GPIOC 的模式配置寄存器，效果对应寄存器版
     * CRH 里把 PC13 配成普通推挽输出。
     */
    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

    /*
     * PA1/PA2 是普通输出。当前 44 课只用 PA1 做 heartbeat，
     * PA2 先配好但不参与任务，避免学生误以为这里有第三个任务。
     */
    gpio.Pin = GPIO_PIN_1 | GPIO_PIN_2;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio);

    /*
     * PA0 配成上拉输入，底层意图对应寄存器版 CNF0=10 且 ODR0=1。
     * 本课不读取 PA0，它只是保留给后续输入/控制类课程。
     */
    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &gpio);
}

void vApplicationMallocFailedHook(void)
{
    /* TCB 或任务栈从 FreeRTOS heap 分配失败时进入这里。 */
    taskDISABLE_INTERRUPTS();
    while (1) {
    }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    /* 参数能帮助定位出问题的任务；本最小例程只停住方便调试。 */
    (void)task;
    (void)task_name;

    taskDISABLE_INTERRUPTS();
    while (1) {
    }
}

static void led_task(void *arg)
{
    (void)arg;

    while (1) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);

        /* RTOS 任务里用 vTaskDelay() 进入阻塞态，而不是 HAL_Delay() 空等。 */
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void heartbeat_task(void *arg)
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
     * xTaskCreate() 成功后，FreeRTOS 会为任务分配 TCB 和任务栈。
     * led_task 优先级 2 更高，但它每 500ms 阻塞一次，所以 beat 仍能运行。
     */
    BaseType_t led_ok = xTaskCreate(led_task, "led", 128, NULL, 2, NULL);
    BaseType_t beat_ok = xTaskCreate(heartbeat_task, "beat", 128, NULL, 1, NULL);

    if ((led_ok != pdPASS) || (beat_ok != pdPASS)) {
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

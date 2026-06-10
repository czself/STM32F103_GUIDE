#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

/*
 * HAL 版：任务创建与删除。
 *
 * HAL 只负责把 PC13 配好；本课真正观察的是 worker 任务
 * 被 creator 创建、运行、阻塞、删除，再下一轮重新创建。
 */

static TaskHandle_t g_worker;

static void system_clock_72mhz_init(void);
static void gpio_init(void);
static void error_stop(void);

static void system_clock_72mhz_init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    /*
     * HAL 字段仍然是在描述 RCC 寄存器意图：
     * HSE 作为 PLL 输入，PLL x9，最后给 FreeRTOS 提供 72MHz CPU 时钟。
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
     * SYSCLK/HCLK/PCLK1/PCLK2 对应时钟树分频字段。
     * 任务延时是否准，最终依赖这里和 FreeRTOSConfig.h 的 72MHz 一致。
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

    /* PC13 只证明 worker 正在运行，不参与任务创建/删除决策。 */
    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

    /* PA1/PA2 在本课不是任务对象，只是已经配好的普通输出脚。 */
    gpio.Pin = GPIO_PIN_1 | GPIO_PIN_2;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* PA0 上拉输入当前不用，别把它误认为 worker 的触发源。 */
    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &gpio);
}

void vApplicationMallocFailedHook(void)
{
    /*
     * xTaskCreate(worker_task, ...) 失败常见原因就是 heap 不够。
     * 本课反复创建 worker，若删除/回收路径有问题，也可能逐渐耗尽 heap。
     */
    taskDISABLE_INTERRUPTS();
    while (1) {
    }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    /*
     * 栈溢出不是 GPIO 问题，而是某个任务自己的栈空间不够。
     * 当前没有串口打印 task_name，所以先保留参数并停在这里供调试器查看。
     */
    (void)task;
    (void)task_name;

    taskDISABLE_INTERRUPTS();
    while (1) {
    }
}

static void worker_task(void *arg)
{
    (void)arg;

    for (uint8_t i = 0; i < 6U; i++) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    /* 删除前清掉句柄，否则 creator 会误以为 worker 仍然存在。 */
    g_worker = NULL;
    vTaskDelete(NULL);
}

static void creator_task(void *arg)
{
    (void)arg;

    while (1) {
        if (g_worker == NULL) {
            BaseType_t create_ok = xTaskCreate(worker_task,
                                               "work",
                                               128,
                                               NULL,
                                               2,
                                               &g_worker);
            if (create_ok != pdPASS) {
                taskDISABLE_INTERRUPTS();
                while (1) {
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

int main(void)
{
    HAL_Init();
    system_clock_72mhz_init();
    gpio_init();

    /*
     * main 只创建长期存在的 creator。
     * 短生命周期 worker 由 creator 在调度器运行后动态创建。
     */
    BaseType_t creator_ok = xTaskCreate(creator_task,
                                        "make",
                                        160,
                                        NULL,
                                        1,
                                        NULL);

    if (creator_ok != pdPASS) {
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

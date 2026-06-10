#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/*
 * HAL 版：FreeRTOS 二值信号量与计数信号量。
 *
 * 50 已经讲过队列负责传数据。本课换成信号量：
 * - 二值信号量表达“事件来了/没来”，不携带数据
 * - 计数信号量表达“还有几份同类资源可用”
 *
 * HAL 只负责 GPIO 现象输出；信号量不是 HAL 对象，也不是 STM32 外设寄存器。
 */

static SemaphoreHandle_t g_binary_sem;
static SemaphoreHandle_t g_counting_sem;

static void system_clock_72mhz_init(void);
static void gpio_init(void);
static void stop_for_debug(void);

static void system_clock_72mhz_init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    /*
     * 任务延时依赖 FreeRTOS tick，tick 又依赖真实 CPU 时钟。
     * 这里用 HSE + PLL x9 得到 72MHz，对齐 FreeRTOSConfig.h。
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

    /*
     * PC13 显示 taker_task 是否拿到二值信号量。
     * HAL_GPIO_Init() 底层会配置 GPIOC 模式寄存器为普通推挽输出。
     */
    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

    /*
     * PA1 显示 worker_task 是否拿到并归还计数信号量的一份资源。
     * PA2 当前只是预留输出，不代表第三个信号量。
     */
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

static void stop_for_debug(void)
{
    taskDISABLE_INTERRUPTS();
    while (1) {
    }
}

void vApplicationMallocFailedHook(void)
{
    /*
     * 信号量对象、任务 TCB、任务栈都从 FreeRTOS heap 分配。
     * heap 不够时会停在这里。
     */
    stop_for_debug();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;
    stop_for_debug();
}

static void giver_task(void *arg)
{
    (void)arg;

    while (1) {
        /*
         * 二值信号量只有 0/1 状态。
         * give 后如果 taker_task 正阻塞等待，它会被唤醒；这里不传递数据值。
         */
        xSemaphoreGive(g_binary_sem);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void taker_task(void *arg)
{
    (void)arg;

    while (1) {
        /*
         * portMAX_DELAY 表示一直等待事件。
         * 没有信号量时本任务阻塞，不会轮询浪费 CPU。
         */
        if (xSemaphoreTake(g_binary_sem, portMAX_DELAY) == pdTRUE) {
            HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        }
    }
}

static void worker_task(void *arg)
{
    (void)arg;

    while (1) {
        /*
         * 计数信号量表示资源数量。
         * 本课最大计数为 2，初始也为 2，所以一开始有两份资源可拿。
         */
        if (xSemaphoreTake(g_counting_sem, portMAX_DELAY) == pdTRUE) {
            HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_1);

            /* 模拟占用资源 300ms。真实工程里这里可能是访问外设或缓冲区。 */
            vTaskDelay(pdMS_TO_TICKS(300));

            /*
             * 用完必须归还。若漏掉 give，计数会逐渐降到 0，
             * 后续 worker 会永久阻塞在 take 上。
             */
            xSemaphoreGive(g_counting_sem);
        }
    }
}

int main(void)
{
    HAL_Init();
    system_clock_72mhz_init();
    gpio_init();

    g_binary_sem = xSemaphoreCreateBinary();
    g_counting_sem = xSemaphoreCreateCounting(2, 2);

    BaseType_t giver_ok = xTaskCreate(giver_task, "give", 128, NULL, 2, NULL);
    BaseType_t taker_ok = xTaskCreate(taker_task, "take", 128, NULL, 1, NULL);
    BaseType_t worker_ok = xTaskCreate(worker_task, "work", 128, NULL, 1, NULL);

    if ((g_binary_sem == NULL) ||
        (g_counting_sem == NULL) ||
        (giver_ok != pdPASS) ||
        (taker_ok != pdPASS) ||
        (worker_ok != pdPASS)) {
        stop_for_debug();
    }

    vTaskStartScheduler();

    stop_for_debug();
}

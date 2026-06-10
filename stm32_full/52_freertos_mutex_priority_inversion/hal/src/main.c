#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/*
 * HAL 版：互斥量与优先级反转。
 *
 * HAL 只负责 GPIO 输出。互斥量是 FreeRTOS 对象，用来表达共享资源所有权，
 * 并通过优先级继承缓解高优先级任务等待低优先级持锁者的问题。
 */

static SemaphoreHandle_t g_mutex;

static void system_clock_72mhz_init(void);
static void gpio_init(void);
static void stop_for_debug(void);

static void system_clock_72mhz_init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    /*
     * 任务延时和优先级反转现象都依赖 FreeRTOS tick。
     * tick 的准确性来自真实 72MHz CPU 时钟，所以这里要和 FreeRTOSConfig.h 对齐。
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

    /* 这组 HAL 字段对应 RCC CFGR 里的系统时钟来源和 AHB/APB 分频。 */
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
     * PC13 显示 high_task 成功拿到互斥量。
     * 它不是共享资源本身，只是 high 进入资源区后的观察点。
     */
    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

    /*
     * PA1 表示 low 持有互斥量，PA2 表示 mid 周期运行。
     * 三个引脚分别对应三个任务阶段，不能混成普通“闪灯任务”理解。
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
    /* 互斥量对象、任务 TCB 和任务栈都消耗 FreeRTOS heap。 */
    stop_for_debug();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    /* 栈溢出会让系统停住，和优先级反转造成的等待延迟不是同一类现象。 */
    (void)task;
    (void)task_name;
    stop_for_debug();
}

static void low_task(void *arg)
{
    (void)arg;

    while (1) {
        if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
            /*
             * low 成为互斥量所有者，表示共享资源现在归 low 使用。
             * 这里翻转 PA1 是为了显示 low 进入了受保护区域。
             */
            HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_1);

            /*
             * 故意持锁延时 600ms，用来放大 high 等待 low 的过程。
             * 真实工程里不要在持有互斥量时做长延时或阻塞 I/O。
             */
            vTaskDelay(pdMS_TO_TICKS(600));

            /* 释放互斥量后，等待它的 high_task 才能继续运行。 */
            xSemaphoreGive(g_mutex);
        }

        /* 释放后稍等，避免 low 马上再次抢占互斥量，让现象更容易观察。 */
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

static void mid_task(void *arg)
{
    (void)arg;

    while (1) {
        /*
         * mid 不碰互斥量，只模拟一个中优先级任务周期运行。
         * 它的存在是为了观察优先级反转背景。
         */
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_2);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void high_task(void *arg)
{
    (void)arg;

    while (1) {
        /*
         * high 先等 200ms，是为了让 low 先拿到互斥量。
         * 如果 high 一启动就 take，它可能先拿到资源，本课现象就不成立。
         */
        vTaskDelay(pdMS_TO_TICKS(200));

        if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
            /* PC13 翻转表示 high 等到了互斥量并进入资源区。 */
            HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
            xSemaphoreGive(g_mutex);
        }
    }
}

int main(void)
{
    HAL_Init();
    system_clock_72mhz_init();
    gpio_init();

    g_mutex = xSemaphoreCreateMutex();

    /*
     * 优先级阶梯是本课的核心条件：
     * low=1 持有资源，mid=2 制造中间干扰，high=3 等待资源。
     */
    BaseType_t low_ok = xTaskCreate(low_task, "low", 128, NULL, 1, NULL);
    BaseType_t mid_ok = xTaskCreate(mid_task, "mid", 128, NULL, 2, NULL);
    BaseType_t high_ok = xTaskCreate(high_task, "high", 128, NULL, 3, NULL);

    if ((g_mutex == NULL) ||
        (low_ok != pdPASS) ||
        (mid_ok != pdPASS) ||
        (high_ok != pdPASS)) {
        stop_for_debug();
    }

    vTaskStartScheduler();

    stop_for_debug();
}

#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/*
 * HAL 版：FreeRTOS 队列集。
 *
 * HAL 负责 GPIO 输出，队列和队列集仍然是 FreeRTOS 内核对象。
 * 本课重点是 selector 同时等待 g_a 和 g_b，而不是 producer 自己处理 LED。
 */

static QueueHandle_t g_a;
static QueueHandle_t g_b;
static QueueSetHandle_t g_set;

static void system_clock_72mhz_init(void);
static void gpio_init(void);
static void stop_for_debug(void);

static void system_clock_72mhz_init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    /* producer 的 700ms/1100ms 节奏依赖 FreeRTOS tick，tick 依赖真实系统时钟。 */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        stop_for_debug();
    }

    /* HAL_RCC_ClockConfig() 根据这些字段配置 RCC 和 Flash 等待周期。 */
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

    /* PC13 显示 selector 从 g_a 取到消息后的处理结果。 */
    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

    /* PA1 显示 selector 从 g_b 取到消息后的处理结果；PA2 当前不用。 */
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
    /* 两个队列、一个队列集、三个任务都会占用 FreeRTOS heap。 */
    stop_for_debug();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;
    stop_for_debug();
}

static void prod_a(void *arg)
{
    const uint8_t value = 1U;

    (void)arg;

    while (1) {
        /*
         * producer A 只负责投递到 g_a，不直接控制 PC13。
         * 这样 selector 才是唯一处理多个来源的任务。
         */
        (void)xQueueSend(g_a, &value, 0);
        vTaskDelay(pdMS_TO_TICKS(700));
    }
}

static void prod_b(void *arg)
{
    const uint8_t value = 2U;

    (void)arg;

    while (1) {
        /* producer B 使用独立队列 g_b，周期不同，用来制造第二个事件来源。 */
        (void)xQueueSend(g_b, &value, 0);
        vTaskDelay(pdMS_TO_TICKS(1100));
    }
}

static void select_task(void *arg)
{
    QueueSetMemberHandle_t active;
    uint8_t value;

    (void)arg;

    while (1) {
        /*
         * 队列集返回的是“哪个成员可读”，不是队列里的 value。
         * 所以下面必须再到对应原队列调用 xQueueReceive()。
         */
        active = xQueueSelectFromSet(g_set, portMAX_DELAY);

        if (active == g_a) {
            /* 队列集已经说明 g_a 可读，所以这里接收等待时间用 0。 */
            if (xQueueReceive(g_a, &value, 0) == pdPASS) {
                HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
            }
        } else if (active == g_b) {
            /* 这里同样是从原队列 g_b 取数据，不是从 g_set 取数据。 */
            if (xQueueReceive(g_b, &value, 0) == pdPASS) {
                HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_1);
            }
        } else {
            stop_for_debug();
        }
    }
}

int main(void)
{
    HAL_Init();
    system_clock_72mhz_init();
    gpio_init();

    g_a = xQueueCreate(4, sizeof(uint8_t));
    g_b = xQueueCreate(4, sizeof(uint8_t));

    /*
     * 队列集容量按成员可能积压的通知总数计算：
     * g_a 长度 4 + g_b 长度 4 = 8。
     */
    g_set = xQueueCreateSet(8);

    if ((g_a == NULL) || (g_b == NULL) || (g_set == NULL)) {
        stop_for_debug();
    }

    if (xQueueAddToSet(g_a, g_set) != pdPASS) {
        stop_for_debug();
    }

    /*
     * 忘记把成员加入队列集时，队列本身可能有数据，
     * 但 selector 永远等不到这个成员的可读通知。
     */
    if (xQueueAddToSet(g_b, g_set) != pdPASS) {
        stop_for_debug();
    }

    /* selector 优先级高于两个 producer，消息到来后能尽快清空对应队列。 */
    BaseType_t a_ok = xTaskCreate(prod_a, "pa", 128, NULL, 1, NULL);
    BaseType_t b_ok = xTaskCreate(prod_b, "pb", 128, NULL, 1, NULL);
    BaseType_t sel_ok = xTaskCreate(select_task, "sel", 160, NULL, 2, NULL);

    if ((a_ok != pdPASS) || (b_ok != pdPASS) || (sel_ok != pdPASS)) {
        stop_for_debug();
    }

    vTaskStartScheduler();

    stop_for_debug();
}

#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"

/*
 * HAL 版：FreeRTOS 事件组。
 *
 * HAL 只替换 GPIO 初始化和翻转方式；事件组不是 HAL 对象。
 * 本课等待的是 BIT_A 和 BIT_B 都到齐，而不是等待某个 GPIO 电平。
 */

#define BIT_A (1U << 0)
#define BIT_B (1U << 1)

static EventGroupHandle_t g_events;

static void system_clock_72mhz_init(void);
static void gpio_init(void);
static void stop_for_debug(void);

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

static void stop_for_debug(void)
{
    taskDISABLE_INTERRUPTS();
    while (1) {
    }
}

void vApplicationMallocFailedHook(void)
{
    stop_for_debug();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;
    stop_for_debug();
}

static void task_a(void *arg)
{
    (void)arg;

    while (1) {
        xEventGroupSetBits(g_events, BIT_A);
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_1);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void task_b(void *arg)
{
    (void)arg;

    while (1) {
        xEventGroupSetBits(g_events, BIT_B);
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_2);
        vTaskDelay(pdMS_TO_TICKS(900));
    }
}

static void wait_task(void *arg)
{
    (void)arg;

    while (1) {
        /*
         * clearOnExit=pdTRUE，waitForAllBits=pdTRUE：
         * 两个 bit 到齐后返回，并清掉这轮已经满足的条件。
         */
        (void)xEventGroupWaitBits(g_events,
                                  BIT_A | BIT_B,
                                  pdTRUE,
                                  pdTRUE,
                                  portMAX_DELAY);

        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    }
}

int main(void)
{
    HAL_Init();
    system_clock_72mhz_init();
    gpio_init();

    g_events = xEventGroupCreate();

    BaseType_t a_ok = xTaskCreate(task_a, "a", 128, NULL, 1, NULL);
    BaseType_t b_ok = xTaskCreate(task_b, "b", 128, NULL, 1, NULL);
    BaseType_t wait_ok = xTaskCreate(wait_task, "wait", 160, NULL, 2, NULL);

    if ((g_events == NULL) ||
        (a_ok != pdPASS) ||
        (b_ok != pdPASS) ||
        (wait_ok != pdPASS)) {
        stop_for_debug();
    }

    vTaskStartScheduler();

    stop_for_debug();
}

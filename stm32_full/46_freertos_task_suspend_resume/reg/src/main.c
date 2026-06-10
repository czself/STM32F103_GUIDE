#include "stm32f1xx.h"
#include "FreeRTOS.h"
#include "task.h"

/*
 * 寄存器版：任务挂起与恢复。
 *
 * 45 已经学过任务创建/删除。本课新重点是：
 * - vTaskDelay() 让任务进入阻塞态，到时间会自动回来
 * - vTaskSuspend() 让任务进入挂起态，不会靠时间自动回来
 * - vTaskResume() 必须拿到任务句柄，才能恢复指定任务
 */

static TaskHandle_t g_blink;

static void system_clock_72mhz_init(void)
{
    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;
    RCC->CR |= RCC_CR_HSEON;
    while ((RCC->CR & RCC_CR_HSERDY) == 0U) {
    }

    RCC->CFGR &= ~(RCC_CFGR_HPRE |
                   RCC_CFGR_PPRE1 |
                   RCC_CFGR_PPRE2 |
                   RCC_CFGR_PLLSRC |
                   RCC_CFGR_PLLXTPRE |
                   RCC_CFGR_PLLMULL |
                   RCC_CFGR_SW);
    RCC->CFGR |= RCC_CFGR_HPRE_DIV1;
    RCC->CFGR |= RCC_CFGR_PPRE1_DIV2;
    RCC->CFGR |= RCC_CFGR_PPRE2_DIV1;
    RCC->CFGR |= RCC_CFGR_PLLSRC;
    RCC->CFGR |= RCC_CFGR_PLLMULL9;

    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0U) {
    }

    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {
    }
}

static void gpio_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN |
                    RCC_APB2ENR_IOPAEN |
                    RCC_APB2ENR_AFIOEN;

    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
    GPIOC->CRH |= GPIO_CRH_MODE13_1;
    GPIOC->BSRR = GPIO_BSRR_BS13;

    GPIOA->CRL &= ~(GPIO_CRL_MODE0 |
                    GPIO_CRL_CNF0 |
                    GPIO_CRL_MODE1 |
                    GPIO_CRL_CNF1 |
                    GPIO_CRL_MODE2 |
                    GPIO_CRL_CNF2);
    GPIOA->CRL |= GPIO_CRL_CNF0_1;
    GPIOA->CRL |= GPIO_CRL_MODE1_1;
    GPIOA->CRL |= GPIO_CRL_MODE2_1;
    GPIOA->BSRR = GPIO_BSRR_BS0;
}

static void led_toggle_pc13(void)
{
    if ((GPIOC->ODR & GPIO_ODR_ODR13) != 0U) {
        GPIOC->BRR = GPIO_BRR_BR13;
    } else {
        GPIOC->BSRR = GPIO_BSRR_BS13;
    }
}

void vApplicationMallocFailedHook(void)
{
    /* 两个任务的 TCB/栈创建失败会停在这里，不要误判成挂起逻辑问题。 */
    taskDISABLE_INTERRUPTS();
    while (1) {
    }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    /* 若 blink 或 control 栈不足，系统会停在这里，而不是正常暂停/恢复。 */
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
        led_toggle_pc13();

        /*
         * 这是阻塞态：200ms 到期后，blink 会自动回到就绪态。
         * 它和 control_task 后面制造的“挂起态”不是一回事。
         */
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void control_task(void *arg)
{
    (void)arg;

    while (1) {
        /*
         * 挂起 blink 后，它不会因为自己的 200ms delay 到期而运行。
         * 这一步需要 g_blink 句柄指向那个具体任务。
         */
        vTaskSuspend(g_blink);

        vTaskDelay(pdMS_TO_TICKS(1000));

        /*
         * 恢复后，blink 才重新回到可调度路径。
         * 所以肉眼看到的是闪烁暂停一段时间，再继续闪烁。
         */
        vTaskResume(g_blink);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

int main(void)
{
    system_clock_72mhz_init();
    gpio_init();

    /*
     * blink 的句柄必须保存到 g_blink。
     * control_task 后面要用这个句柄把“具体哪个任务”挂起和恢复。
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

#include "stm32f1xx.h"
#include "FreeRTOS.h"
#include "task.h"

/*
 * 寄存器版：FreeRTOS 任务创建与删除。
 *
 * 44 已经证明调度器能跑。本课新重点是任务生命周期：
 * creator_task 长期存在，周期性创建 worker_task；
 * worker_task 短暂闪灯后删除自己，把运行权交回系统。
 */

static TaskHandle_t g_worker;

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
    /* GPIO 仍然只做现象输出：PC13 显示 worker_task 是否正在运行。 */
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
    /* 反复创建任务时若 heap 不足，会停在这里。 */
    taskDISABLE_INTERRUPTS();
    while (1) {
    }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    /*
     * worker 反复创建/删除时，若任务栈给得太小，可能在运行中停到这里。
     * task_name 本来可以指出是哪一个任务；当前课程没有串口，所以先停住。
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

    /*
     * worker 是短生命周期任务。
     * 它只闪 6 次，然后删除自己；这和普通函数 return 不同。
     */
    for (uint8_t i = 0; i < 6U; i++) {
        led_toggle_pc13();
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    /*
     * 先清句柄，告诉 creator 下一轮可以重新创建 worker。
     * 再删除当前任务。参数 NULL 表示删除“我自己”。
     */
    g_worker = NULL;
    vTaskDelete(NULL);
}

static void creator_task(void *arg)
{
    (void)arg;

    while (1) {
        if (g_worker == NULL) {
            /*
             * xTaskCreate() 不是直接调用 worker_task。
             * 它向 FreeRTOS 申请 TCB/栈，并把 worker 放进就绪列表。
             */
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
    system_clock_72mhz_init();
    gpio_init();

    /*
     * creator 长期存在，所以栈给 160，比短小的 worker 稍大。
     * 它的职责不是闪灯，而是周期性创建 worker 这个 RTOS 对象。
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

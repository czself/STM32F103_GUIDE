#include "stm32f1xx.h"
#include "FreeRTOS.h"
#include "task.h"

/*
 * 寄存器版：FreeRTOS 入门移植与基础调度。
 *
 * 时钟和 GPIO 在前面已经学过，这里只保留必要配置。
 * 本课新重点是最小 RTOS 链路：
 * - 创建两个任务
 * - 启动调度器
 * - 任务用 vTaskDelay() 阻塞让出 CPU
 */

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
    /*
     * GPIO 只是任务运行的可见证据：
     * - PC13 由 led_task 翻转
     * - PA1 由 heartbeat_task 翻转
     * - PA0 保持上拉输入，给后续课程留观察点
     */
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

static void led_toggle_pa1(void)
{
    if ((GPIOA->ODR & GPIO_ODR_ODR1) != 0U) {
        GPIOA->BRR = GPIO_BRR_BR1;
    } else {
        GPIOA->BSRR = GPIO_BSRR_BS1;
    }
}

void vApplicationMallocFailedHook(void)
{
    /*
     * xTaskCreate() 会从 FreeRTOS heap 分配 TCB 和任务栈。
     * heap 不够时停在这里，说明任务可能根本没有创建成功。
     */
    taskDISABLE_INTERRUPTS();
    while (1) {
    }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    /*
     * 任务栈太小或任务里使用了过多局部空间时会进入这里。
     * 当前示例任务很短，若进 hook，优先检查栈深度和配置。
     */
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
        led_toggle_pc13();

        /*
         * vTaskDelay() 不是空转延时。
         * 当前任务进入阻塞态，调度器可以运行 heartbeat_task 或空闲任务。
         */
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void heartbeat_task(void *arg)
{
    (void)arg;

    while (1) {
        led_toggle_pa1();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

int main(void)
{
    system_clock_72mhz_init();
    gpio_init();

    /*
     * 栈深度 128 的单位是 StackType_t 个数，不是简单 128 字节。
     * 优先级数字越大越高，所以 led_task 的 2 高于 heartbeat_task 的 1。
     */
    BaseType_t led_ok = xTaskCreate(led_task, "led", 128, NULL, 2, NULL);
    BaseType_t beat_ok = xTaskCreate(heartbeat_task, "beat", 128, NULL, 1, NULL);

    if ((led_ok != pdPASS) || (beat_ok != pdPASS)) {
        taskDISABLE_INTERRUPTS();
        while (1) {
        }
    }

    /*
     * 启动调度器后，CPU 进入任务调度世界。
     * 正常情况下不会再回到 main() 后面的 while。
     */
    vTaskStartScheduler();

    while (1) {
    }
}

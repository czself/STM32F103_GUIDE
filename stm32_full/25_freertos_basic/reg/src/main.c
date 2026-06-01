#include "stm32f1xx.h"
#include "FreeRTOS.h"
#include "task.h"

/*
 * ============================================================================
 * 寄存器版 FreeRTOS 基础：多任务调度
 * ============================================================================
 *
 * ██████  本课核心知识点 ██████
 *
 * 1. RTOS（Real-Time Operating System，实时操作系统）
 *    - FreeRTOS 是嵌入式领域最常用的 RTOS 之一
 *    - 核心功能：让多个"任务"轮流运行，每个任务看起来像独立的函数
 *    - 裸机用一个 while(1) 处理所有事务；RTOS 把事务拆成多个任务
 *
 * 2. 任务（Task）
 *    - 任务 = 一个永远不返回的函数（内部有 while(1) 循环）
 *    - 每个任务有独立的栈空间、优先级和状态
 *    - FreeRTOS 调度器（Scheduler）负责切换哪个任务运行
 *
 * 3. vTaskDelay() —— 任务阻塞
 *    - 让当前任务进入"阻塞态"一段时间（如 500ms）
 *    - 阻塞期间 CPU 不运行这个任务，而是运行其他就绪任务
 *    - 普通的 HAL_Delay(500) 是忙等待，CPU 全程空跑
 *    - vTaskDelay(500) 是真正的阻塞，CPU 可以做别的事
 *
 * 4. xTaskCreate() —— 任务创建
 *    - 参数：函数指针、任务名、栈大小、参数、优先级、任务句柄
 *    - 只是"登记任务"到内核，真正运行要等调度器启动
 *
 * 5. vTaskStartScheduler() —— 启动调度器
 *    - 调用后 FreeRTOS 开始用 SysTick 中断进行任务切换
 *    - 程序控制权交给调度器，正常情况下不会回到 vTaskStartScheduler 之后
 *
 * 6. 优先级和调度
 *    - FreeRTOS 的优先级数字越大，优先级越高（和 NVIC 相反！）
 *    - 相同优先级的任务：时间片轮转（configUSE_TIME_SLICING=1）
 *    - 高优先级任务可以完全抢占低优先级任务
 *
 * 7. FreeRTOSConfig.h 关键配置
 *    - configCPU_CLOCK_HZ = 72000000（CPU 时钟 72MHz）
 *    - configTICK_RATE_HZ = 1000（Tick 周期 1ms，即 SysTick 中断频率）
 *    - configMAX_PRIORITIES = 5（可用优先级 0~4，数字越大越高）
 *    - configTOTAL_HEAP_SIZE = 12KB（FreeRTOS 动态分配堆大小）
 *
 * 8. 为什么需要 vApplicationMallocFailedHook()
 *    - FreeRTOS 使用内置的 heap_4 内存分配器
 *    - 如果 heap 不足（12KB 不够分配任务栈），会调用这个钩子函数
 *    - 实现中关闭中断进入死循环，便于调试器定位问题
 */

static void system_clock_72mhz_init(void)
{
    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;
    RCC->CR |= RCC_CR_HSEON;
    while ((RCC->CR & RCC_CR_HSERDY) == 0U) {
    }
    RCC->CFGR &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2 |
                   RCC_CFGR_PLLSRC | RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMULL |
                   RCC_CFGR_SW);
    RCC->CFGR |= RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 |
                 RCC_CFGR_PPRE2_DIV1 | RCC_CFGR_PLLSRC | RCC_CFGR_PLLMULL9;
    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0U) {
    }
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {
    }
}

static void gpio_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN | RCC_APB2ENR_IOPAEN;

    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
    GPIOC->CRH |= GPIO_CRH_MODE13_1;
    GPIOC->BSRR = GPIO_BSRR_BS13;

    GPIOA->CRL &= ~(GPIO_CRL_MODE1 | GPIO_CRL_CNF1);
    GPIOA->CRL |= GPIO_CRL_MODE1_1;
    GPIOA->BRR = GPIO_BRR_BR1;
}

static void toggle_pc13(void)
{
    if ((GPIOC->ODR & GPIO_ODR_ODR13) != 0U) GPIOC->BRR = GPIO_BRR_BR13;
    else GPIOC->BSRR = GPIO_BSRR_BS13;
}

static void toggle_pa1(void)
{
    if ((GPIOA->ODR & GPIO_ODR_ODR1) != 0U) GPIOA->BRR = GPIO_BRR_BR1;
    else GPIOA->BSRR = GPIO_BSRR_BS1;
}

static void led_task(void *argument)
{
    (void)argument;
    while (1) {
        toggle_pc13();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void heartbeat_task(void *argument)
{
    (void)argument;
    while (1) {
        toggle_pa1();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    while (1) {
    }
}

int main(void)
{
    system_clock_72mhz_init();
    gpio_init();

    xTaskCreate(led_task, "led", 128, NULL, 2, NULL);
    xTaskCreate(heartbeat_task, "beat", 128, NULL, 1, NULL);

    vTaskStartScheduler();

    while (1) {
    }
}

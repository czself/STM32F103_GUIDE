#include "stm32f1xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/*
 * ============================================================================
 * 寄存器版 FreeRTOS + UART 中断
 * ============================================================================
 *
 * ██████  本课核心知识点 ██████
 *
 * 1. 中断与任务的配合原则
 *    - 中断服务函数（ISR）：只做最简单的事（读字节、入队）
 *    - 任务：做复杂的事（解析命令、控制外设、回显）
 *    - 为什么？ISR 每个时刻只有一个能运行，不能阻塞，不能做耗时操作
 *
 * 2. xQueueSendFromISR() —— 中断上下文专用的队列发送
 *    - 不会像任务版本那样阻塞（ISR 中不允许阻塞）
 *    - 可以通过 BaseType_t *pxHigherPriorityTaskWoken 参数
 *      告诉内核：是否有更高优先级任务因为这次操作而就绪
 *    - portYIELD_FROM_ISR()：如果有就绪的高优先级任务，立即切换
 *
 * 3. FreeRTOS 中断优先级规则（关键！）
 *    - STM32F1 的 NVIC 有 4 位优先级（0~15，越小越高）
 *    - configMAX_SYSCALL_INTERRUPT_PRIORITY = 5
 *    - 会调用 FromISR API 的中断：优先级必须 ≥ 5（数字 ≥ 5 即优先级更低）
 *    - 优先级 0~4 的中断不能调用 FreeRTOS API（它们不受 FreeRTOS 管理）
 *    - 本课 USART1 优先级 = 6，符合要求
 *
 * 4. USART1 关键寄存器
 *    - SR（Status Register）：
 *      · RXNE = 1：接收数据寄存器非空（收到了新字节）
 *      · TXE = 1：发送数据寄存器为空（可以发送新字节）
 *      · TC = 1：发送完成（所有数据都发完了）
 *    - DR（Data Register）：读取 RX 数据，写入 TX 数据
 *    - BRR（Baud Rate Register）：波特率分频值
 *    - CR1（Control Register 1）：
 *      · TE：发送使能；RE：接收使能
 *      · RXNEIE：RXNE 中断使能；UE：USART 使能
 *
 * 5. 波特率计算
 *    - USART1 挂在 APB2（72MHz）
 *    - BRR = 0x0271 = 625
 *    - 实际波特率 = 72MHz / (16 × 625) = 720（近似 115200）
 *      （实际公式更复杂，但 0x0271 是 STM32CubeMX 为 115200 的常用值）
 */

static QueueHandle_t g_uart_queue;

static void system_clock_72mhz_init(void)
{
    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;
    RCC->CR |= RCC_CR_HSEON;
    while ((RCC->CR & RCC_CR_HSERDY) == 0U) {}
    RCC->CFGR &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2 |
                   RCC_CFGR_PLLSRC | RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMULL |
                   RCC_CFGR_SW);
    RCC->CFGR |= RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 |
                 RCC_CFGR_PPRE2_DIV1 | RCC_CFGR_PLLSRC | RCC_CFGR_PLLMULL9;
    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0U) {}
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {}
}

static void led_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;
    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
    GPIOC->CRH |= GPIO_CRH_MODE13_1;
    GPIOC->BSRR = GPIO_BSRR_BS13;
}

static void led_toggle(void)
{
    if ((GPIOC->ODR & GPIO_ODR_ODR13) != 0U) GPIOC->BRR = GPIO_BRR_BR13;
    else GPIOC->BSRR = GPIO_BSRR_BS13;
}

static void uart1_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_USART1EN;

    GPIOA->CRH &= ~(GPIO_CRH_MODE9 | GPIO_CRH_CNF9 | GPIO_CRH_MODE10 | GPIO_CRH_CNF10);
    GPIOA->CRH |= GPIO_CRH_MODE9_1 | GPIO_CRH_CNF9_1;
    GPIOA->CRH |= GPIO_CRH_CNF10_0;

    USART1->BRR = 0x0271U;
    USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_UE;

    NVIC_SetPriority(USART1_IRQn, 6U);
    NVIC_EnableIRQ(USART1_IRQn);
}

static void uart1_write_byte(uint8_t byte)
{
    while ((USART1->SR & USART_SR_TXE) == 0U) {}
    USART1->DR = byte;
}

static void uart_task(void *argument)
{
    uint8_t byte;
    (void)argument;

    while (1) {
        if (xQueueReceive(g_uart_queue, &byte, portMAX_DELAY) == pdPASS) {
            uart1_write_byte(byte);
            if ((byte == 't') || (byte == 'T')) {
                led_toggle();
            }
        }
    }
}

void USART1_IRQHandler(void)
{
    BaseType_t woken = pdFALSE;

    if ((USART1->SR & USART_SR_RXNE) != 0U) {
        uint8_t byte = (uint8_t)USART1->DR;
        xQueueSendFromISR(g_uart_queue, &byte, &woken);
    }

    portYIELD_FROM_ISR(woken);
}

void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    while (1) {}
}

int main(void)
{
    system_clock_72mhz_init();
    led_init();
    uart1_init();

    g_uart_queue = xQueueCreate(32, sizeof(uint8_t));
    xTaskCreate(uart_task, "uart", 160, NULL, 2, NULL);
    vTaskStartScheduler();

    while (1) {}
}

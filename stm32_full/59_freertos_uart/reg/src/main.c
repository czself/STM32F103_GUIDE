#include "stm32f1xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/*
 * 寄存器版：FreeRTOS + USART1 中断队列。
 *
 * 本课新重点是“中断只搬运字节，任务处理业务”：
 * - USART1_IRQHandler 只读取 DR，并用 xQueueSendFromISR() 投递到队列
 * - uart_task 阻塞等待队列，收到字节后回显，并判断 t/T 是否翻转 LED
 *
 * 这样 ISR 不解析命令、不阻塞发送、不做长循环；串口业务留给任务执行。
 */

static QueueHandle_t g_uart_queue;

static void stop_for_debug(void)
{
    taskDISABLE_INTERRUPTS();
    while (1) {
    }
}

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

static void led_init(void)
{
    /* PC13 只作为串口命令 t/T 的可见反馈。 */
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;

    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
    GPIOC->CRH |= GPIO_CRH_MODE13_1;
    GPIOC->BSRR = GPIO_BSRR_BS13;
}

static void led_toggle(void)
{
    if ((GPIOC->ODR & GPIO_ODR_ODR13) != 0U) {
        GPIOC->BRR = GPIO_BRR_BR13;
    } else {
        GPIOC->BSRR = GPIO_BSRR_BS13;
    }
}

static void uart1_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_USART1EN;

    /*
     * PA9 = USART1_TX：复用推挽输出。
     * PA10 = USART1_RX：输入，用来接 USB-TTL 发来的串口波形。
     */
    GPIOA->CRH &= ~(GPIO_CRH_MODE9 |
                    GPIO_CRH_CNF9 |
                    GPIO_CRH_MODE10 |
                    GPIO_CRH_CNF10);
    GPIOA->CRH |= GPIO_CRH_MODE9_1;
    GPIOA->CRH |= GPIO_CRH_CNF9_1;
    GPIOA->CRH |= GPIO_CRH_CNF10_0;

    /*
     * APB2 = 72MHz 时，0x0271 是 F1 USART1 115200 8N1 的常用 BRR 编码。
     * BRR 包含整数分频和小数分频，不是把 0x0271 当十进制除数直接相除。
     */
    USART1->BRR = 0x0271U;

    /*
     * TE/RE 打开发送和接收，RXNEIE 允许收到字节时进中断，UE 最后使能 USART。
     */
    USART1->CR1 = USART_CR1_TE |
                  USART_CR1_RE |
                  USART_CR1_RXNEIE |
                  USART_CR1_UE;

    /*
     * 本中断会调用 FreeRTOS FromISR API。
     * 在本课程配置下优先级数字 6 低于 max syscall 禁止线，可以安全调用。
     */
    NVIC_SetPriority(USART1_IRQn, 6U);
    NVIC_EnableIRQ(USART1_IRQn);
}

static void uart1_write_byte(uint8_t byte)
{
    while ((USART1->SR & USART_SR_TXE) == 0U) {
    }

    USART1->DR = byte;
}

static void uart_task(void *argument)
{
    uint8_t byte;

    (void)argument;

    while (1) {
        /*
         * 没有串口字节时阻塞，不空转占 CPU。
         * ISR 每收到一个字节，会把这个任务从阻塞态唤醒。
         */
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
        /*
         * 读取 DR 会消费本次 RXNE 事件。
         * ISR 只把字节送进队列，命令解析留给 uart_task。
         */
        uint8_t byte = (uint8_t)USART1->DR;
        (void)xQueueSendFromISR(g_uart_queue, &byte, &woken);
    }

    portYIELD_FROM_ISR(woken);
}

void vApplicationMallocFailedHook(void)
{
    /* 队列对象、任务 TCB 和任务栈都来自 FreeRTOS heap。 */
    stop_for_debug();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;

    stop_for_debug();
}

int main(void)
{
    system_clock_72mhz_init();
    led_init();
    uart1_init();

    /*
     * 队列长度 32，元素大小 1 字节。
     * 短突发串口输入可以先缓存在队列里，任务再按自己的节奏处理。
     */
    g_uart_queue = xQueueCreate(32, sizeof(uint8_t));

    BaseType_t uart_ok = xTaskCreate(uart_task,
                                     "uart",
                                     160,
                                     NULL,
                                     2,
                                     NULL);

    if ((g_uart_queue == NULL) || (uart_ok != pdPASS)) {
        stop_for_debug();
    }

    vTaskStartScheduler();

    stop_for_debug();
}

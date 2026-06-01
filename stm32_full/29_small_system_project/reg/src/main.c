#include "stm32f1xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/*
 * ============================================================================
 * 寄存器版小系统综合项目
 * ============================================================================
 *
 * ██████  本课核心知识点 ██████
 *
 * 1. 系统架构设计
 *    - 不是把所有代码塞进一个 while(1)
 *    - 而是按职责拆分为多个任务，每个任务只关心自己的事
 *    - 事件通过队列流动，任务之间不直接访问共享变量
 *
 * 2. 事件驱动设计模式
 *    - g_event_queue：事件队列（按键事件、UART 命令事件）
 *    - g_uart_queue：UART 字节队列（中断→任务的桥梁）
 *    - EVENT_KEY = 1：按键按下事件
 *    - EVENT_UART_TOGGLE = 2：UART 命令 't'/'T' 事件
 *
 * 3. 任务职责划分
 *    | 任务        | 职责                         | 优先级 |
 *    |------------|------------------------------|--------|
 *    | adc_task   | 周期 100ms 读取 PA0 电压      |   2    |
 *    | key_task   | 周期 20ms 轮询 PB0 按键       |   2    |
 *    | control_task| 等待事件，翻转 LED，打印日志  |   2    |
 *    | uart_task  | 接收串口字节，解析 t/s 命令    |   2    |
 *    | status_task| 每 2 秒打印一次 ADC 值         |   1    |
 *
 * 4. 中断边界设计
 *    - USART1 中断只读取字节，放进 g_uart_queue
 *    - 命令解析（'t'/'T'/'s'/'S'）在 uart_task 中完成
 *    - 这是 RTOS 系统设计中最重要的原则之一：
 *      中断做最少的事，任务做复杂的逻辑
 *
 * 5. 共享变量保护
 *    - g_adc_value 被 adc_task 写入，被 uart_task 和 status_task 读取
 *    - 在本例中，uint16_t 的读写在 ARM Cortex-M3 上是原子的
 *    - 但如果读写更复杂的结构体，需要用互斥锁（mutex）
 *
 * 6. 设计思路回顾
 *    先想清楚数据流，再写代码：
 *    输入（按键、串口、ADC）→ 事件/数据 → 处理（控制任务）→ 输出（LED、串口）
 */

enum { EVENT_KEY = 1, EVENT_UART_TOGGLE = 2 };

static QueueHandle_t g_event_queue;
static QueueHandle_t g_uart_queue;
static volatile uint16_t g_adc_value;

static void system_clock_72mhz_init(void)
{
    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;
    RCC->CR |= RCC_CR_HSEON;
    while ((RCC->CR & RCC_CR_HSERDY) == 0U) {}
    RCC->CFGR &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2 |
                   RCC_CFGR_PLLSRC | RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMULL |
                   RCC_CFGR_SW | RCC_CFGR_ADCPRE);
    RCC->CFGR |= RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 |
                 RCC_CFGR_PPRE2_DIV1 | RCC_CFGR_PLLSRC | RCC_CFGR_PLLMULL9 |
                 RCC_CFGR_ADCPRE_DIV6;
    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0U) {}
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {}
}

static void gpio_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN |
                    RCC_APB2ENR_IOPCEN | RCC_APB2ENR_AFIOEN;

    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
    GPIOC->CRH |= GPIO_CRH_MODE13_1;
    GPIOC->BSRR = GPIO_BSRR_BS13;

    GPIOB->CRL &= ~(GPIO_CRL_MODE0 | GPIO_CRL_CNF0);
    GPIOB->CRL |= GPIO_CRL_CNF0_1;
    GPIOB->BSRR = GPIO_BSRR_BS0;

    GPIOA->CRL &= ~(GPIO_CRL_MODE0 | GPIO_CRL_CNF0);
}

static void uart1_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN | RCC_APB2ENR_IOPAEN;
    GPIOA->CRH &= ~(GPIO_CRH_MODE9 | GPIO_CRH_CNF9 | GPIO_CRH_MODE10 | GPIO_CRH_CNF10);
    GPIOA->CRH |= GPIO_CRH_MODE9_1 | GPIO_CRH_CNF9_1 | GPIO_CRH_CNF10_0;
    USART1->BRR = 0x0271U;
    USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_UE;
    NVIC_SetPriority(USART1_IRQn, 6U);
    NVIC_EnableIRQ(USART1_IRQn);
}

static void adc1_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    ADC1->SMPR2 = ADC_SMPR2_SMP0;
    ADC1->SQR1 = 0U;
    ADC1->SQR3 = 0U;
    ADC1->CR2 = ADC_CR2_ADON;
    for (volatile uint32_t i = 0; i < 10000U; i++) {}
    ADC1->CR2 |= ADC_CR2_CAL;
    while ((ADC1->CR2 & ADC_CR2_CAL) != 0U) {}
}

static uint16_t adc1_read(void)
{
    ADC1->CR2 |= ADC_CR2_ADON;
    while ((ADC1->SR & ADC_SR_EOC) == 0U) {}
    return (uint16_t)ADC1->DR;
}

static void led_toggle(void)
{
    if ((GPIOC->ODR & GPIO_ODR_ODR13) != 0U) GPIOC->BRR = GPIO_BRR_BR13;
    else GPIOC->BSRR = GPIO_BSRR_BS13;
}

static uint8_t button_pressed(void)
{
    return (GPIOB->IDR & GPIO_IDR_IDR0) == 0U;
}

static void uart_write_byte(uint8_t byte)
{
    while ((USART1->SR & USART_SR_TXE) == 0U) {}
    USART1->DR = byte;
}

static void uart_write_str(const char *s)
{
    while (*s != '\0') {
        uart_write_byte((uint8_t)*s++);
    }
}

static void uart_write_u16(uint16_t value)
{
    char buf[6];
    int i = 0;
    if (value == 0U) {
        uart_write_byte('0');
        return;
    }
    while (value > 0U) {
        buf[i++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while (i > 0) uart_write_byte((uint8_t)buf[--i]);
}

static void adc_task(void *argument)
{
    (void)argument;
    while (1) {
        g_adc_value = adc1_read();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void key_task(void *argument)
{
    uint8_t last = 0U;
    (void)argument;
    while (1) {
        uint8_t now = button_pressed();
        if ((now != 0U) && (last == 0U)) {
            uint8_t event = EVENT_KEY;
            xQueueSend(g_event_queue, &event, 0);
        }
        last = now;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void control_task(void *argument)
{
    uint8_t event;
    (void)argument;
    while (1) {
        if (xQueueReceive(g_event_queue, &event, portMAX_DELAY) == pdPASS) {
            led_toggle();
            uart_write_str(event == EVENT_KEY ? "key\r\n" : "toggle\r\n");
        }
    }
}

static void uart_task(void *argument)
{
    uint8_t byte;
    (void)argument;
    uart_write_str("system ready\r\n");
    while (1) {
        if (xQueueReceive(g_uart_queue, &byte, portMAX_DELAY) == pdPASS) {
            if ((byte == 't') || (byte == 'T')) {
                uint8_t event = EVENT_UART_TOGGLE;
                xQueueSend(g_event_queue, &event, 0);
            } else if ((byte == 's') || (byte == 'S')) {
                uart_write_str("adc=");
                uart_write_u16(g_adc_value);
                uart_write_str("\r\n");
            }
        }
    }
}

static void status_task(void *argument)
{
    (void)argument;
    while (1) {
        uart_write_str("adc=");
        uart_write_u16(g_adc_value);
        uart_write_str("\r\n");
        vTaskDelay(pdMS_TO_TICKS(2000));
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
    gpio_init();
    uart1_init();
    adc1_init();

    g_event_queue = xQueueCreate(8, sizeof(uint8_t));
    g_uart_queue = xQueueCreate(32, sizeof(uint8_t));

    xTaskCreate(adc_task, "adc", 128, NULL, 2, NULL);
    xTaskCreate(key_task, "key", 128, NULL, 2, NULL);
    xTaskCreate(control_task, "ctrl", 192, NULL, 2, NULL);
    xTaskCreate(uart_task, "uart", 192, NULL, 2, NULL);
    xTaskCreate(status_task, "stat", 192, NULL, 1, NULL);
    vTaskStartScheduler();

    while (1) {}
}

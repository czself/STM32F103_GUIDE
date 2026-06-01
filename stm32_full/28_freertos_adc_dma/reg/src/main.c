#include "stm32f1xx.h"
#include "FreeRTOS.h"
#include "task.h"

/*
 * ============================================================================
 * 寄存器版 FreeRTOS + ADC + DMA
 * ============================================================================
 *
 * ██████  本课核心知识点 ██████
 *
 * 1. DMA（Direct Memory Access，直接内存访问）
 *    - DMA 可以在没有 CPU 介入的情况下搬运数据
 *    - ADC 转换完成后，数据自动存入内存缓冲区
 *    - CPU 只需要在缓冲区满/半满时处理数据，不浪费在等待 ADC 上
 *
 * 2. ADC 关键寄存器
 *    - CR1：控制寄存器 1（扫描模式、中断使能等）
 *    - CR2：控制寄存器 2（DMA使能、连续模式、ADON启动）
 *    - SQR1：序列长度
 *    - SQR3：序列中的通道号（第一转换通道）
 *    - SMPR2：采样时间（通道 0~9 的采样周期）
 *    - SR：状态寄存器（EOC：转换完成）
 *    - DR：数据寄存器（12 位转换结果）
 *
 * 3. ADC 时钟配置
 *    - ADC 时钟来自 APB2 的分频
 *    - RCC_CFGR.ADCPRE = DIV6 → ADC_CLK = 72MHz / 6 = 12MHz
 *    - ADC 最大时钟不超过 14MHz，所以必须分频
 *
 * 4. DMA 通道与 ADC 的映射
 *    - STM32F1 的 DMA 通道和外设是固定映射的
 *    - ADC1 → DMA1_Channel1（不能选择其他通道！）
 *
 * 5. 任务通知（Task Notification）—— 比队列更轻量
 *    - vTaskNotifyGiveFromISR()：中断里给任务发通知（+1）
 *    - ulTaskNotifyTake()：任务等待通知（可阻塞）
 *    - 通知不传递数据，只传递"事件发生了"这个信号
 *    - 比创建队列更节省内存
 *
 * 6. ADC 校准（必须做！）
 *    - 上电后先写 ADON（第一次 = 唤醒 ADC），等待一段时间
 *    - 再写 CAL（启动校准），等待 CAL 清零
 *    - 不做校准，ADC 值可能有几十 LSB 的偏差
 */

#define ADC_BUF_LEN 16U

static volatile uint16_t g_adc_buf[ADC_BUF_LEN];
static TaskHandle_t g_adc_task_handle;

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

static void led_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;
    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
    GPIOC->CRH |= GPIO_CRH_MODE13_1;
    GPIOC->BSRR = GPIO_BSRR_BS13;
}

static void adc_dma_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_ADC1EN;
    RCC->AHBENR |= RCC_AHBENR_DMA1EN;

    GPIOA->CRL &= ~(GPIO_CRL_MODE0 | GPIO_CRL_CNF0);

    DMA1_Channel1->CCR = 0U;
    DMA1_Channel1->CPAR = (uint32_t)&ADC1->DR;
    DMA1_Channel1->CMAR = (uint32_t)g_adc_buf;
    DMA1_Channel1->CNDTR = ADC_BUF_LEN;
    DMA1_Channel1->CCR = DMA_CCR_MINC | DMA_CCR_CIRC |
                          DMA_CCR_PSIZE_0 | DMA_CCR_MSIZE_0 |
                          DMA_CCR_HTIE | DMA_CCR_TCIE;

    NVIC_SetPriority(DMA1_Channel1_IRQn, 6U);
    NVIC_EnableIRQ(DMA1_Channel1_IRQn);

    ADC1->CR2 = ADC_CR2_DMA | ADC_CR2_CONT;
    ADC1->SMPR2 = ADC_SMPR2_SMP0;
    ADC1->SQR1 = 0U;
    ADC1->SQR3 = 0U;

    ADC1->CR2 |= ADC_CR2_ADON;
    for (volatile uint32_t i = 0; i < 10000U; i++) {}
    ADC1->CR2 |= ADC_CR2_CAL;
    while ((ADC1->CR2 & ADC_CR2_CAL) != 0U) {}

    DMA1_Channel1->CCR |= DMA_CCR_EN;
    ADC1->CR2 |= ADC_CR2_ADON;
}

static void adc_task(void *argument)
{
    (void)argument;
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        uint32_t sum = 0U;
        for (uint32_t i = 0; i < ADC_BUF_LEN; i++) {
            sum += g_adc_buf[i];
        }

        if ((sum / ADC_BUF_LEN) > 2048U) GPIOC->BRR = GPIO_BRR_BR13;
        else GPIOC->BSRR = GPIO_BSRR_BS13;
    }
}

void DMA1_Channel1_IRQHandler(void)
{
    BaseType_t woken = pdFALSE;

    if ((DMA1->ISR & DMA_ISR_HTIF1) != 0U) {
        DMA1->IFCR = DMA_IFCR_CHTIF1;
        vTaskNotifyGiveFromISR(g_adc_task_handle, &woken);
    }
    if ((DMA1->ISR & DMA_ISR_TCIF1) != 0U) {
        DMA1->IFCR = DMA_IFCR_CTCIF1;
        vTaskNotifyGiveFromISR(g_adc_task_handle, &woken);
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

    xTaskCreate(adc_task, "adc", 160, NULL, 2, &g_adc_task_handle);
    adc_dma_init();
    vTaskStartScheduler();

    while (1) {}
}

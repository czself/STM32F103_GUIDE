#include "stm32f1xx.h"
#include "FreeRTOS.h"
#include "task.h"

/*
 * 寄存器版：FreeRTOS + ADC1 + DMA1_Channel1。
 *
 * 本课新链路是三层协作：
 * - ADC1 连续把 PA0 电压转换到 ADC1->DR
 * - DMA1_Channel1 自动把 DR 搬到 g_adc_buf
 * - DMA 半满/全满中断只通知 adc_task，平均值计算放到任务里做
 */

#define ADC_BUF_LEN 16U

static volatile uint16_t g_adc_buf[ADC_BUF_LEN];
static TaskHandle_t g_adc_task_handle;

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
                   RCC_CFGR_SW |
                   RCC_CFGR_ADCPRE);
    RCC->CFGR |= RCC_CFGR_HPRE_DIV1;
    RCC->CFGR |= RCC_CFGR_PPRE1_DIV2;
    RCC->CFGR |= RCC_CFGR_PPRE2_DIV1;
    RCC->CFGR |= RCC_CFGR_PLLSRC;
    RCC->CFGR |= RCC_CFGR_PLLMULL9;
    RCC->CFGR |= RCC_CFGR_ADCPRE_DIV6;

    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0U) {
    }

    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {
    }
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

    /* PA0 清 MODE/CNF 后是模拟输入，数字输入缓冲不会干扰 ADC 采样。 */
    GPIOA->CRL &= ~(GPIO_CRL_MODE0 | GPIO_CRL_CNF0);

    /*
     * ADC1 在 STM32F103 上固定映射到 DMA1_Channel1。
     * CPAR 指向外设数据寄存器，CMAR 指向内存数组，方向由 CCR 默认外设到内存。
     */
    DMA1_Channel1->CCR = 0U;
    DMA1_Channel1->CPAR = (uint32_t)&ADC1->DR;
    DMA1_Channel1->CMAR = (uint32_t)g_adc_buf;
    DMA1_Channel1->CNDTR = ADC_BUF_LEN;
    DMA1_Channel1->CCR = DMA_CCR_MINC |
                          DMA_CCR_CIRC |
                          DMA_CCR_PSIZE_0 |
                          DMA_CCR_MSIZE_0 |
                          DMA_CCR_HTIE |
                          DMA_CCR_TCIE;

    NVIC_SetPriority(DMA1_Channel1_IRQn, 6U);
    NVIC_EnableIRQ(DMA1_Channel1_IRQn);

    /*
     * CR2.DMA 允许 ADC 转换完成后发 DMA 请求。
     * CR2.CONT 让 ADC 连续转换；DMA 循环模式负责反复填充缓冲区。
     */
    ADC1->CR2 = ADC_CR2_DMA | ADC_CR2_CONT;
    ADC1->SMPR2 = ADC_SMPR2_SMP0;
    ADC1->SQR1 = 0U;
    ADC1->SQR3 = 0U;

    ADC1->CR2 |= ADC_CR2_ADON;
    for (volatile uint32_t i = 0; i < 10000U; i++) {
    }

    ADC1->CR2 |= ADC_CR2_CAL;
    while ((ADC1->CR2 & ADC_CR2_CAL) != 0U) {
    }

    DMA1_Channel1->CCR |= DMA_CCR_EN;
    ADC1->CR2 |= ADC_CR2_ADON;
}

static void adc_task(void *argument)
{
    (void)argument;

    while (1) {
        /*
         * pdTRUE 表示拿到通知后把通知计数清零。
         * DMA 中断可能来自半满或全满，本任务醒来后统一求整个缓冲平均值。
         */
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        uint32_t sum = 0U;
        for (uint32_t i = 0; i < ADC_BUF_LEN; i++) {
            sum += g_adc_buf[i];
        }

        if ((sum / ADC_BUF_LEN) > 2048U) {
            GPIOC->BRR = GPIO_BRR_BR13;
        } else {
            GPIOC->BSRR = GPIO_BSRR_BS13;
        }
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
    /* adc_task 的 TCB/栈来自 FreeRTOS heap，分配失败停在这里。 */
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

    /*
     * 先创建任务并保存句柄，再打开 DMA 中断。
     * 否则 DMA 中断来得太早时，FromISR 通知没有目标任务。
     */
    BaseType_t adc_ok = xTaskCreate(adc_task,
                                    "adc",
                                    160,
                                    NULL,
                                    2,
                                    &g_adc_task_handle);

    if ((adc_ok != pdPASS) || (g_adc_task_handle == NULL)) {
        stop_for_debug();
    }

    adc_dma_init();
    vTaskStartScheduler();

    stop_for_debug();
}

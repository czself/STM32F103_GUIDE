#include "stm32f1xx_hal.h"

#include <string.h>

/*
 * HAL 版：DMA 内存拷贝与 USART1 发送。
 *
 * 本课 HAL 版故意保留两个层次：
 * - memcpy() 表示普通 CPU 内存拷贝，便于和 DMA 搬运概念对比
 * - HAL_UART_Transmit_DMA() 表示 USART1_TX 通过 DMA1_Channel4 发送
 */

static UART_HandleTypeDef huart1;
static DMA_HandleTypeDef hdma_tx;

static uint8_t g_src[16] = "DMA UART demo\n";
static uint8_t g_dst[16];

static void system_clock_72mhz_init(void);
static void pc13_led_init(void);
static void uart_dma_init(void);
static void error_handler(void);

int main(void)
{
    HAL_Init();
    system_clock_72mhz_init();
    pc13_led_init();
    uart_dma_init();

    while (1) {
        memcpy(g_dst, g_src, sizeof(g_dst));

        if (HAL_UART_Transmit_DMA(&huart1, g_dst, sizeof(g_dst)) != HAL_OK) {
            error_handler();
        }

        HAL_Delay(200);
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        HAL_Delay(800);
    }
}

static void uart_dma_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    /*
     * PA9 是 USART1_TX，必须是复用推挽输出。
     */
    gpio.Pin = GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart1) != HAL_OK) {
        error_handler();
    }

    /*
     * USART1_TX 对应 DMA1_Channel4。
     * Direction=MEMORY_TO_PERIPH 对应寄存器版 DIR=1。
     */
    hdma_tx.Instance = DMA1_Channel4;
    hdma_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_tx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_tx.Init.Mode = DMA_NORMAL;
    hdma_tx.Init.Priority = DMA_PRIORITY_LOW;

    if (HAL_DMA_Init(&hdma_tx) != HAL_OK) {
        error_handler();
    }

    /*
     * 这句不是装饰代码。
     * 它把 huart1.hdmatx 指向 hdma_tx，让 HAL_UART_Transmit_DMA()
     * 能找到该使用哪个 DMA 通道。
     */
    __HAL_LINKDMA(&huart1, hdmatx, hdma_tx);

    /*
     * HAL_UART_Transmit_DMA() 使用中断回调收尾：
     * - DMA1_Channel4_IRQHandler 让 HAL 知道 DMA 已经搬完
     * - USART1_IRQHandler 让 HAL 等最后一个停止位发完后恢复 READY
     *
     * 少了这两道中断链，第一轮可能发出去了，但下一轮会因为
     * huart1 仍处于 BUSY_TX 而启动失败。
     */
    HAL_NVIC_SetPriority(DMA1_Channel4_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel4_IRQn);
    HAL_NVIC_SetPriority(USART1_IRQn, 1, 1);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
}

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
        error_handler();
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
        error_handler();
    }
}

static void pc13_led_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);

    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
}

void SysTick_Handler(void)
{
    HAL_IncTick();
}

void DMA1_Channel4_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_tx);
}

void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1);
}

static void error_handler(void)
{
    __disable_irq();
    while (1) {
    }
}

#include "stm32f1xx_hal.h"

static void system_clock_72mhz_init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};
    /* HAL 结构体只是把寄存器配置换成字段；这里仍然是在选择 HSE 和 PLL x9。 */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL9;
    HAL_RCC_OscConfig(&osc);
    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2);
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

#include <string.h>
static UART_HandleTypeDef huart1;
static DMA_HandleTypeDef hdma_tx;
static uint8_t g_src[16] = "DMA UART demo\n";
static uint8_t g_dst[16];
static void uart_dma_init(void)
{
    GPIO_InitTypeDef gpio = {0}; __HAL_RCC_GPIOA_CLK_ENABLE(); __HAL_RCC_USART1_CLK_ENABLE(); __HAL_RCC_DMA1_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_9; gpio.Mode = GPIO_MODE_AF_PP; gpio.Speed = GPIO_SPEED_FREQ_HIGH; HAL_GPIO_Init(GPIOA, &gpio);
    huart1.Instance=USART1; huart1.Init.BaudRate=115200; huart1.Init.WordLength=UART_WORDLENGTH_8B; huart1.Init.StopBits=UART_STOPBITS_1; huart1.Init.Parity=UART_PARITY_NONE; huart1.Init.Mode=UART_MODE_TX; huart1.Init.HwFlowCtl=UART_HWCONTROL_NONE; huart1.Init.OverSampling=UART_OVERSAMPLING_16; HAL_UART_Init(&huart1);
    hdma_tx.Instance=DMA1_Channel4; hdma_tx.Init.Direction=DMA_MEMORY_TO_PERIPH; hdma_tx.Init.PeriphInc=DMA_PINC_DISABLE; hdma_tx.Init.MemInc=DMA_MINC_ENABLE; hdma_tx.Init.PeriphDataAlignment=DMA_PDATAALIGN_BYTE; hdma_tx.Init.MemDataAlignment=DMA_MDATAALIGN_BYTE; hdma_tx.Init.Mode=DMA_NORMAL; hdma_tx.Init.Priority=DMA_PRIORITY_LOW; HAL_DMA_Init(&hdma_tx); __HAL_LINKDMA(&huart1, hdmatx, hdma_tx);
}
int main(void){ HAL_Init(); system_clock_72mhz_init(); pc13_led_init(); uart_dma_init(); while(1){ memcpy(g_dst,g_src,sizeof(g_dst)); HAL_UART_Transmit_DMA(&huart1,g_dst,sizeof(g_dst)); HAL_Delay(200); HAL_GPIO_TogglePin(GPIOC,GPIO_PIN_13); HAL_Delay(800); } }

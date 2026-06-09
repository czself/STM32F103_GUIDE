#include "stm32f1xx_hal.h"

static SPI_HandleTypeDef hspi1; static void system_clock_72mhz_init(void); static void pc13_led_init(void); static void spi1_init(void);
static uint8_t spi_xfer(uint8_t b){uint8_t r=0;HAL_SPI_TransmitReceive(&hspi1,&b,&r,1,100);return r;}
static uint8_t w25q64_read_mid(void){uint8_t id;HAL_GPIO_WritePin(GPIOA,GPIO_PIN_4,GPIO_PIN_RESET);spi_xfer(0x9F);id=spi_xfer(0xFF);spi_xfer(0xFF);spi_xfer(0xFF);HAL_GPIO_WritePin(GPIOA,GPIO_PIN_4,GPIO_PIN_SET);return id;}
int main(void){HAL_Init();system_clock_72mhz_init();pc13_led_init();spi1_init();while(1){uint8_t mid=w25q64_read_mid();HAL_GPIO_TogglePin(GPIOC,GPIO_PIN_13);HAL_Delay((mid==0xEFU)?100:500);}}
static void spi1_init(void){GPIO_InitTypeDef gpio={0};__HAL_RCC_GPIOA_CLK_ENABLE();__HAL_RCC_SPI1_CLK_ENABLE();gpio.Pin=GPIO_PIN_4;gpio.Mode=GPIO_MODE_OUTPUT_PP;gpio.Speed=GPIO_SPEED_FREQ_HIGH;HAL_GPIO_Init(GPIOA,&gpio);HAL_GPIO_WritePin(GPIOA,GPIO_PIN_4,GPIO_PIN_SET);gpio.Pin=GPIO_PIN_5|GPIO_PIN_7;gpio.Mode=GPIO_MODE_AF_PP;HAL_GPIO_Init(GPIOA,&gpio);gpio.Pin=GPIO_PIN_6;gpio.Mode=GPIO_MODE_INPUT;gpio.Pull=GPIO_NOPULL;HAL_GPIO_Init(GPIOA,&gpio);hspi1.Instance=SPI1;hspi1.Init.Mode=SPI_MODE_MASTER;hspi1.Init.Direction=SPI_DIRECTION_2LINES;hspi1.Init.DataSize=SPI_DATASIZE_8BIT;hspi1.Init.CLKPolarity=SPI_POLARITY_LOW;hspi1.Init.CLKPhase=SPI_PHASE_1EDGE;hspi1.Init.NSS=SPI_NSS_SOFT;hspi1.Init.BaudRatePrescaler=SPI_BAUDRATEPRESCALER_8;hspi1.Init.FirstBit=SPI_FIRSTBIT_MSB;hspi1.Init.TIMode=SPI_TIMODE_DISABLE;hspi1.Init.CRCCalculation=SPI_CRCCALCULATION_DISABLE;hspi1.Init.CRCPolynomial=7;HAL_SPI_Init(&hspi1);}
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
        while (1) {
        }
    }

    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) {
        while (1) {
        }
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

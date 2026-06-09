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

#define SCL_PIN GPIO_PIN_6
#define SDA_PIN GPIO_PIN_7
static void i2c_delay(void){ for(volatile uint32_t i=0;i<120U;i++){ __NOP(); } }
static void bus(GPIO_PinState scl, GPIO_PinState sda){ HAL_GPIO_WritePin(GPIOB,SCL_PIN,scl); HAL_GPIO_WritePin(GPIOB,SDA_PIN,sda); i2c_delay(); }
static void i2c_start(void){ bus(GPIO_PIN_SET,GPIO_PIN_SET); bus(GPIO_PIN_SET,GPIO_PIN_RESET); bus(GPIO_PIN_RESET,GPIO_PIN_RESET); }
static void i2c_stop(void){ bus(GPIO_PIN_RESET,GPIO_PIN_RESET); bus(GPIO_PIN_SET,GPIO_PIN_RESET); bus(GPIO_PIN_SET,GPIO_PIN_SET); }
static void i2c_write_byte(uint8_t b){ for(uint8_t i=0;i<8;i++){ GPIO_PinState s=(b&0x80U)?GPIO_PIN_SET:GPIO_PIN_RESET; bus(GPIO_PIN_RESET,s); bus(GPIO_PIN_SET,s); b<<=1; } bus(GPIO_PIN_RESET,GPIO_PIN_SET); bus(GPIO_PIN_SET,GPIO_PIN_SET); bus(GPIO_PIN_RESET,GPIO_PIN_SET); }
static void soft_i2c_init(void){ GPIO_InitTypeDef gpio={0}; __HAL_RCC_GPIOB_CLK_ENABLE(); gpio.Pin=SCL_PIN|SDA_PIN; gpio.Mode=GPIO_MODE_OUTPUT_OD; gpio.Pull=GPIO_PULLUP; gpio.Speed=GPIO_SPEED_FREQ_HIGH; HAL_GPIO_Init(GPIOB,&gpio); bus(GPIO_PIN_SET,GPIO_PIN_SET); }
int main(void){ HAL_Init(); system_clock_72mhz_init(); pc13_led_init(); soft_i2c_init(); while(1){ i2c_start(); i2c_write_byte(0xA0U); i2c_write_byte(0U); i2c_write_byte(0x5AU); i2c_stop(); HAL_GPIO_TogglePin(GPIOC,GPIO_PIN_13); HAL_Delay(1000); } }

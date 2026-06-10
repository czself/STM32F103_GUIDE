#include "stm32f1xx_hal.h"

/*
 * HAL 版：I2C 读写 MPU6050 寄存器。
 *
 * HAL_I2C_Mem_Write/Read 把“器件地址 + 内部寄存器地址 + 数据”封装起来。
 * 本课要看清楚：MPU6050 的 0x6B、0x75 都是器件内部寄存器地址。
 */

#define MPU_ADDR 0xD0U

static I2C_HandleTypeDef hi2c1;

static void system_clock_72mhz_init(void);
static void pc13_led_init(void);
static void i2c1_init(void);
static void error_handler(void);

int main(void)
{
    uint8_t id = 0U;
    uint8_t wake_value = 0U;

    HAL_Init();
    system_clock_72mhz_init();
    pc13_led_init();
    i2c1_init();
    HAL_Delay(50);

    /*
     * PWR_MGMT_1 = 0x6B。
     * 写 0x00 表示唤醒 MPU6050，否则 WHO_AM_I 之外的数据功能可能仍处于睡眠状态。
     */
    if (HAL_I2C_Mem_Write(&hi2c1,
                          MPU_ADDR,
                          0x6BU,
                          I2C_MEMADD_SIZE_8BIT,
                          &wake_value,
                          1U,
                          100U) != HAL_OK) {
        error_handler();
    }

    while (1) {
        /*
         * WHO_AM_I = 0x75。
         * 正常模块通常返回 0x68，用它做 I2C 通信自检。
         */
        if (HAL_I2C_Mem_Read(&hi2c1,
                             MPU_ADDR,
                             0x75U,
                             I2C_MEMADD_SIZE_8BIT,
                             &id,
                             1U,
                             100U) != HAL_OK) {
            error_handler();
        }

        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        HAL_Delay((id == 0x68U) ? 100U : 500U);
    }
}

static void i2c1_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);

    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 100000;
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0U;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0U;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&hi2c1) != HAL_OK) {
        error_handler();
    }
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

static void error_handler(void)
{
    __disable_irq();
    while (1) {
    }
}

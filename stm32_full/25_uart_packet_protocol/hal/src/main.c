#include "stm32f1xx_hal.h"

static UART_HandleTypeDef huart1;
static void system_clock_72mhz_init(void);
static void pc13_led_init(void);
static void usart1_init(void);

static void handle_packet(uint8_t cmd, uint8_t data)
{
    if (cmd == 0x01U) {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, data ? GPIO_PIN_RESET : GPIO_PIN_SET);
    }
}

int main(void)
{
    enum { WAIT_HEAD, WAIT_CMD, WAIT_DATA, WAIT_TAIL } state = WAIT_HEAD;
    uint8_t b, cmd = 0, data = 0;
    HAL_Init();
    system_clock_72mhz_init();
    pc13_led_init();
    usart1_init();
    while (1) {
        if (HAL_UART_Receive(&huart1, &b, 1, HAL_MAX_DELAY) == HAL_OK) {
            switch (state) {
            case WAIT_HEAD: state = (b == 0xAAU) ? WAIT_CMD : WAIT_HEAD; break;
            case WAIT_CMD:  cmd = b; state = WAIT_DATA; break;
            case WAIT_DATA: data = b; state = WAIT_TAIL; break;
            case WAIT_TAIL:
                if (b == 0x55U) handle_packet(cmd, data);
                state = WAIT_HEAD;
                break;
            }
        }
    }
}

static void usart1_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart1);
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

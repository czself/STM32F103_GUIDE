#include "stm32f1xx_hal.h"

/*
 * HAL 版：USART1 数据包协议解析。
 *
 * HAL_UART_Receive() 负责阻塞接收 1 个字节；协议状态机负责判断这个字节
 * 在 0xAA CMD DATA 0x55 四字节帧里的位置。
 *
 * HAL 只是换了接收 API，协议边界仍然必须由我们自己维护。
 */

typedef enum {
    WAIT_HEAD = 0,
    WAIT_CMD,
    WAIT_DATA,
    WAIT_TAIL
} PacketState;

static UART_HandleTypeDef huart1;

static void system_clock_72mhz_init(void);
static void pc13_led_init(void);
static void usart1_init(void);
static void error_handler(void);

static void handle_packet(uint8_t cmd, uint8_t data)
{
    /*
     * CMD=0x01：控制 PC13 LED。
     * BluePill 板载 LED 低电平点亮，所以 data!=0 时写 RESET。
     */
    if (cmd == 0x01U) {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, data ? GPIO_PIN_RESET : GPIO_PIN_SET);
    }
}

static void packet_fsm_input(PacketState *state, uint8_t byte, uint8_t *cmd, uint8_t *data)
{
    switch (*state) {
    case WAIT_HEAD:
        /*
         * 不在帧内时只认包头 0xAA。
         * 其他字节全部丢弃，用来抵抗串口刚接入时的半包和噪声。
         */
        if (byte == 0xAAU) {
            *state = WAIT_CMD;
        }
        break;

    case WAIT_CMD:
        *cmd = byte;
        *state = WAIT_DATA;
        break;

    case WAIT_DATA:
        *data = byte;
        *state = WAIT_TAIL;
        break;

    case WAIT_TAIL:
        /*
         * 包尾正确才执行；包尾错误则整帧丢弃。
         * 执行后也回到 WAIT_HEAD，准备解析下一帧。
         */
        if (byte == 0x55U) {
            handle_packet(*cmd, *data);
        }
        *state = WAIT_HEAD;
        break;

    default:
        *state = WAIT_HEAD;
        break;
    }
}

int main(void)
{
    PacketState state = WAIT_HEAD;
    uint8_t byte = 0U;
    uint8_t cmd = 0U;
    uint8_t data = 0U;

    HAL_Init();
    system_clock_72mhz_init();
    pc13_led_init();
    usart1_init();

    while (1) {
        /*
         * 每次只接收 1 字节，把“串口收字节”和“协议解析”分开。
         * HAL_MAX_DELAY 表示一直等到收到数据，适合这个最小演示。
         */
        if (HAL_UART_Receive(&huart1, &byte, 1U, HAL_MAX_DELAY) == HAL_OK) {
            packet_fsm_input(&state, byte, &cmd, &data);
        }
    }
}

static void usart1_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();

    /*
     * PA9 = USART1_TX，复用推挽输出。
     */
    gpio.Pin = GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    /*
     * PA10 = USART1_RX，输入。
     */
    gpio.Pin = GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200U;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart1) != HAL_OK) {
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

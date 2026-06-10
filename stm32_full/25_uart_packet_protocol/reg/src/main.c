#include "stm32f1xx.h"

/*
 * 寄存器版：USART1 数据包协议解析。
 *
 * 21~24 已经学过 UART 收发、printf 重定向。本课的新重点不是“收到一个字节”，
 * 而是把连续字节流切分成有边界、有含义的一帧数据。
 *
 * 本课约定 4 字节协议帧：
 *   0xAA  CMD  DATA  0x55
 *
 * 关键理解：
 * - UART 本身只给出一个个字节，不知道哪里是包头、哪里是包尾。
 * - 状态机负责记住“当前正在等包头/命令/数据/包尾”。
 * - 任何位置收到不符合预期的字节，都要回到 WAIT_HEAD 重新找包头。
 */

typedef enum {
    WAIT_HEAD = 0,
    WAIT_CMD,
    WAIT_DATA,
    WAIT_TAIL
} PacketState;

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
                   RCC_CFGR_SW);

    RCC->CFGR |= RCC_CFGR_HPRE_DIV1;
    RCC->CFGR |= RCC_CFGR_PPRE1_DIV2;
    RCC->CFGR |= RCC_CFGR_PPRE2_DIV1;
    RCC->CFGR |= RCC_CFGR_PLLSRC;
    RCC->CFGR |= RCC_CFGR_PLLMULL9;

    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0U) {
    }

    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {
    }
}

static void pc13_led_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;

    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
    GPIOC->CRH |= GPIO_CRH_MODE13_1;

    GPIOC->BSRR = GPIO_BSRR_BS13;
}

static void led_set(uint8_t on)
{
    if (on != 0U) {
        GPIOC->BRR = GPIO_BRR_BR13;
    } else {
        GPIOC->BSRR = GPIO_BSRR_BS13;
    }
}

static void usart1_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_USART1EN;

    /*
     * PA9 = USART1_TX，复用推挽输出。
     * PA10 = USART1_RX，浮空输入。本课靠 RX 接收上位机发来的协议帧。
     */
    GPIOA->CRH &= ~(GPIO_CRH_MODE9 |
                    GPIO_CRH_CNF9 |
                    GPIO_CRH_MODE10 |
                    GPIO_CRH_CNF10);

    GPIOA->CRH |= GPIO_CRH_MODE9_1;
    GPIOA->CRH |= GPIO_CRH_CNF9_1;
    GPIOA->CRH |= GPIO_CRH_CNF10_0;

    USART1->BRR = 0x0271; /* 72MHz / 115200 */
    USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

static uint8_t usart1_read_byte(void)
{
    while ((USART1->SR & USART_SR_RXNE) == 0U) {
    }
    return (uint8_t)USART1->DR;
}

static void handle_packet(uint8_t cmd, uint8_t data)
{
    /*
     * CMD=0x01：控制板载 LED。
     * DATA!=0 点亮，DATA=0 熄灭。
     *
     * 这里故意只支持一个命令，先把“协议解析”和“业务动作”分开。
     * 后续如果增加蜂鸣器、PWM、传感器命令，只扩展这个函数即可。
     */
    if (cmd == 0x01U) {
        led_set(data);
    }
}

static void packet_fsm_input(PacketState *state, uint8_t byte, uint8_t *cmd, uint8_t *data)
{
    /*
     * 状态机每次只处理 1 个字节。
     * UART 什么时候来数据由硬件决定；协议解析的责任是把这些字节串成帧。
     */
    switch (*state) {
    case WAIT_HEAD:
        /*
         * 等包头 0xAA。
         * 只要不是包头，就丢弃。这样即使上电时从半包中间开始接收，
         * 也能在下一个 0xAA 到来时重新同步。
         */
        if (byte == 0xAAU) {
            *state = WAIT_CMD;
        }
        break;

    case WAIT_CMD:
        /*
         * 包头后第 1 个字节解释为命令码。
         * 这里先不判断命令是否合法，因为不同命令可能有不同数据含义。
         */
        *cmd = byte;
        *state = WAIT_DATA;
        break;

    case WAIT_DATA:
        /*
         * 命令后的 1 个字节是参数数据。
         * 本课固定长度 4 字节，所以收到 DATA 后就去等包尾。
         */
        *data = byte;
        *state = WAIT_TAIL;
        break;

    case WAIT_TAIL:
        /*
         * 包尾必须是 0x55。
         * 包尾正确才执行命令；包尾错误说明这一帧坏了，直接丢弃并重新等包头。
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
    uint8_t cmd = 0U;
    uint8_t data = 0U;

    system_clock_72mhz_init();
    pc13_led_init();
    usart1_init();

    while (1) {
        uint8_t byte = usart1_read_byte();
        packet_fsm_input(&state, byte, &cmd, &data);
    }
}

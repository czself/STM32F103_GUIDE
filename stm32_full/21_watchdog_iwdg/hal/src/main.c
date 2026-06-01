#include "stm32f1xx_hal.h"

/*
 * ============================================================================
 * HAL 版 IWDG（独立看门狗）实验
 * ============================================================================
 *
 * ██████  HAL API 与寄存器版对照 ██████
 *
 * | HAL API                    | 底层寄存器操作                       |
 * |----------------------------|--------------------------------------|
 * | HAL_IWDG_Init(&hiwdg)      | KR=0x5555→写PR/RLR→等待SR→喂狗→启动 |
 * | HAL_IWDG_Refresh(&hiwdg)   | KR=0xAAAA（重装计数器）              |
 * | __HAL_RCC_GET_FLAG()       | 读 CSR 复位标志位                    |
 * | __HAL_RCC_CLEAR_RESET_FLAGS()| 写 CSR.RMVF                          |
 *
 * HAL 将 IWDG 的"钥匙值操作"封装在 HAL_IWDG_Init() 内部，
 * 用户只需要配置 Prescaler 和 Reload 参数，
 * 不需要手动处理 KR 寄存器的各种魔法值。
 *
 * 但底层原理完全相同：最终都是向 IWDG 寄存器写入同样的值。
 */

static IWDG_HandleTypeDef hiwdg;  /* IWDG 句柄 */

static void gpio_init(void);
static void iwdg_init(void);
static uint8_t key_pressed(void);
static void led_blink(uint32_t times);

int main(void)
{
    uint8_t was_iwdg_reset;

    HAL_Init();
    gpio_init();

    /*
     * 检测复位来源
     * __HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) 读取 CSR 寄存器中的 IWDGRSTF 位
     * 返回 1 表示上一次复位由 IWDG 引起
     *
     * __HAL_RCC_CLEAR_RESET_FLAGS() 对应写 CSR.RMVF 清除所有复位标志
     */
    was_iwdg_reset = __HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) ? 1U : 0U;
    __HAL_RCC_CLEAR_RESET_FLAGS();
    if (was_iwdg_reset) {
        led_blink(3U);  /* IWDG 复位指示：快闪 3 次 */
    }

    iwdg_init();  /* 配置并启动 IWDG */

    while (1) {
        if (key_pressed()) {
            /*
             * HAL_IWDG_Refresh() 底层写 IWDG->KR = 0xAAAA
             * 重装计数器，防止 IWDG 超时复位
             */
            HAL_IWDG_Refresh(&hiwdg);
            led_blink(1U);
        } else {
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);  /* LED 亮 */
        }
        HAL_Delay(300U);
    }
}

static void gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* PC13 推挽输出 */
    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

    /* PA0 上拉输入 */
    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &gpio);
}

static void iwdg_init(void)
{
    /*
     * HAL_IWDG_Init() 内部完成以下序列：
     * 1. IWDG->KR = 0x5555 → 解锁 PR/RLR 写保护
     * 2. 写 IWDG->PR = Prescaler 值（预分频）
     * 3. 写 IWDG->RLR = Reload 值（重装值）
     * 4. 等待 IWDG->SR.PVU 和 SR.RVU 清零
     * 5. IWDG->KR = 0xAAAA → 先喂一次狗
     * 6. IWDG->KR = 0xCCCC → 启动 IWDG
     *
     * 这里只需要指定：
     *   Prescaler = IWDG_PRESCALER_32（预分频 32）
     *   Reload = 2500（重装值）
     *
     * 对应底层：PR=011（分频32），RLR=2500
     * 超时时间 ≈ 2 秒（使用约 40kHz 的 LSI）
     */
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_32;  /* 预分频 32 */
    hiwdg.Init.Reload = 2500U;                  /* 重装值 2500 */
    HAL_IWDG_Init(&hiwdg);                      /* 解锁→配置→启动 */
}

static uint8_t key_pressed(void)
{
    /*
     * HAL_GPIO_ReadPin() 返回 GPIO_PIN_RESET（0）表示低电平
     * PA0 按键按下时接 GND，所以读回 RESET 表示按下
     */
    return HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET;
}

static void led_blink(uint32_t times)
{
    for (uint32_t i = 0; i < times; i++) {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);  /* LED 亮 */
        HAL_Delay(120U);
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);    /* LED 灭 */
        HAL_Delay(120U);
    }
}

void SysTick_Handler(void)
{
    HAL_IncTick();
}
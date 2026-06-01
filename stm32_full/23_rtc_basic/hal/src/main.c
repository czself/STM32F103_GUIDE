#include "stm32f1xx_hal.h"

/*
 * ============================================================================
 * HAL 版 RTC（实时时钟）基础实验
 * ============================================================================
 *
 * ██████  HAL API 与寄存器版对照 ██████
 *
 * | HAL API                                    | 底层寄存器操作                  |
 * |-------------------------------------------|---------------------------------|
 * | HAL_PWR_EnableBkUpAccess()                | PWR->CR |= PWR_CR_DBP          |
 * | HAL_RCC_OscConfig()                       | RCC->CSR |= RCC_CSR_LSION      |
 * | HAL_RCCEx_PeriphCLKConfig()               | RCC->BDCR 选择 RTC 时钟源       |
 * | __HAL_RCC_RTC_ENABLE()                    | RCC->BDCR |= RCC_BDCR_RTCEN    |
 * | HAL_RTC_Init(&hrtc)                       | 配置 PRL 预分频                 |
 * | HAL_RTC_GetTime(&hrtc, &time, BIN)        | 读取 CNTH/CNTL 并换算为时分秒   |
 *
 * HAL 封装了备份域解锁、RSF 同步等待、配置模式进出等细节，
 * 用户只需要配置"预分频"和"时钟源"，更接近工程开发的使用方式。
 */

static RTC_HandleTypeDef hrtc;  /* RTC 句柄 */

static void gpio_init(void);
static void rtc_init(void);
static void led_toggle(void);

int main(void)
{
    RTC_TimeTypeDef time = {0};
    uint8_t last_sec;

    HAL_Init();
    gpio_init();
    rtc_init();

    /*
     * HAL_RTC_GetTime() 内部会：
     * 1. 等待 RSF 同步完成
     * 2. 读取 CNTH/CNTL
     * 3. 根据预分频换算成秒/分/时
     *
     * RTC_FORMAT_BIN 表示返回二进制格式（而不是 BCD）
     * 返回的 time.Seconds 是当前秒数（0~59）
     */
    HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT_BIN);
    last_sec = time.Seconds;

    while (1) {
        HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT_BIN);
        if (time.Seconds != last_sec) {
            /*
             * 秒值变化 → LED 翻转
             * 这等价于寄存器版中比较 rtc_get_counter() 是否变化
             * HAL 内部已处理好高低位一致性和寄存器同步
             */
            last_sec = time.Seconds;
            led_toggle();
        }
    }
}

static void gpio_init(void)
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

static void rtc_init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_PeriphCLKInitTypeDef periph = {0};

    /*
     * HAL_RTC_Init() 内部处理了寄存器版中手动做的多个步骤：
     * 1. HAL_PWR_EnableBkUpAccess() → 解锁备份域
     * 2. HAL_RCC_OscConfig() → 启动 LSI 时钟
     * 3. HAL_RCCEx_PeriphCLKConfig() → 选择 LSI 作为 RTC 时钟
     * 4. __HAL_RCC_RTC_ENABLE() → 使能 RTC
     * 5. HAL_RTC_Init() → 配置预分频 PRL = 40000-1
     */
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_RCC_BKP_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();  /* 解锁备份域 */

    /* 配置 LSI 振荡器 */
    osc.OscillatorType = RCC_OSCILLATORTYPE_LSI;
    osc.LSIState = RCC_LSI_ON;
    osc.PLL.PLLState = RCC_PLL_NONE;  /* LSI 配置不影响 PLL，设为 NONE 跳过 */
    HAL_RCC_OscConfig(&osc);

    /* 选择 LSI 作为 RTC 时钟源 */
    periph.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    periph.RTCClockSelection = RCC_RTCCLKSOURCE_LSI;
    HAL_RCCEx_PeriphCLKConfig(&periph);
    __HAL_RCC_RTC_ENABLE();

    /*
     * 配置 RTC 预分频
     * AsynchPrediv = 40000 - 1 → 40kHz / 40000 = 1Hz（每秒计数一次）
     * OutPut = NONE：不使用 RTC 的校准输出引脚
     */
    hrtc.Instance = RTC;
    hrtc.Init.AsynchPrediv = 40000U - 1U;
    hrtc.Init.OutPut = RTC_OUTPUTSOURCE_NONE;
    HAL_RTC_Init(&hrtc);
}

static void led_toggle(void)
{
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
}

void SysTick_Handler(void)
{
    HAL_IncTick();
}
# 09_pwm_advanced - PWM 进阶

## 1. 本课到底在学什么

本课表面现象是：PA0 外接 LED 呈现呼吸灯效果，从暗到亮，再从亮到暗，循环变化。

真正要学的是“硬件 PWM”和“软件亮度策略”的分工。TIM2_CH1 继续负责输出稳定的 1kHz PWM；主循环不直接制造 PWM 波形，只是周期性修改 `CCR1`，也就是修改占空比。亮度如何变化，则由 `next_duty()` 这个软件函数决定。

和 `08_pwm_basic` 相比，本课的 PWM 频率、PA0 复用输出、PWM mode 1 这些硬件基础基本不变。进阶点在于：上一课用固定步进 `50` 线性改变占空比，本课用分段步进，让暗部、中间亮度、高亮区的变化速度不同，视觉上更像呼吸。

本课还包含一个 `pwm_to_din_test/` 小实验：把普通 1kHz PWM 接到数字灯条 DIN，观察灯条通常不会按正常颜色工作。它用来说明普通 PWM 和 WS2812/SK6812 这类单线数字灯条协议不是一回事。

## 2. 本课学习目标

学完本课，你应该能做到：

- 说明呼吸灯为什么本质上是稳定 PWM 加周期性修改 `CCR1`。
- 区分 PWM 频率、占空比、占空比更新节奏、视觉亮度曲线。
- 解释 `next_duty()` 为什么属于软件策略，而不是定时器硬件模式。
- 看懂低亮度区、中亮度区、高亮度区不同步进对观感的影响。
- 解释 `direction` 为什么用来控制亮度变亮或变暗。
- 说明 `OC1PE` 和 `EGR.UG` 为什么适合 PWM 运行中更新比较值。
- 把 HAL 版 `__HAL_TIM_SET_COMPARE()` 对应回寄存器版 `TIM2->CCR1 = duty`。
- 解释为什么普通 PWM 接数字灯条 DIN 不能等价于发送灯条协议。

## 3. 本课目录结构

```text
09_pwm_advanced/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
├── hal/
│   ├── platformio.ini
│   └── src/main.c
└── pwm_to_din_test/
    ├── platformio.ini
    └── src/main.c
```

`reg/` 用寄存器配置 TIM2_CH1 PWM，并在主循环里直接写 `TIM2->CCR1`。

`hal/` 用 HAL PWM 接口配置 TIM2_CH1，并用 `__HAL_TIM_SET_COMPARE()` 更新比较值。

`pwm_to_din_test/` 是普通 PWM 接数字灯条 DIN 的观察实验。它不是本课主线功能，而是帮助你建立边界：PWM 是周期占空比波形，数字灯条 DIN 要的是严格编码的数据时序。

## 4. 实验硬件

本课主实验使用：

- STM32F103C8T6 BluePill
- ST-Link 下载器
- PA0 引脚
- 220Ω 左右限流电阻
- 普通 LED
- 可选：示波器或逻辑分析仪

推荐接法：

```text
PA0 -> 限流电阻 -> LED 正极
LED 负极 -> GND
```

这种接法下，PA0 高电平时 LED 亮，占空比越大，平均亮度越高。

`pwm_to_din_test/` 如需尝试，需要额外注意供电：

```text
灯条 5V  -> 外部 5V 电源 +
灯条 GND -> 外部 5V 电源 -
STM32 GND -> 外部 5V 电源 -
PA0 -> 220Ω~470Ω -> 灯条 DIN
```

数字灯条不要直接靠 STM32 板子的 3.3V 引脚供整条灯。电源地必须共地，否则 DIN 信号没有共同参考。

## 5. 先建立一个最基本的脑图

本课分成六层来看。

现象层：普通 LED 会缓慢变亮、变暗。示波器上看到的 PA0 仍然是约 1kHz PWM，只是占空比在慢慢变化。

物理/硬件层：PA0 是 TIM2_CH1 的默认复用输出脚。LED 看到 PA0 的快速高低电平，人眼看到的是平均亮度。亮度变化不是因为 PWM 频率变了，而是因为高电平比例变了。

芯片模块层：RCC 提供 GPIOA、AFIO、TIM2 时钟；GPIOA 把 PA0 配成复用推挽；TIM2 生成 PWM；TIM2_CH1 输出通道把 PWM 送到 PA0。

寄存器/bit 层：`PSC` 和 `ARR` 维持 1kHz PWM；`CCR1` 保存当前占空比；`OC1M = 110` 选择 PWM mode 1；`OC1PE` 让比较值更新更规整；`CC1E` 打开通道输出；`CEN` 启动计数器。

C/CMSIS 层：寄存器版用 `TIM2->CCR1 = duty` 更新亮度，用 `next_duty()` 计算下一档 duty，用 `direction` 决定变亮还是变暗。

HAL/工程层：HAL 版用 `HAL_TIM_PWM_Start()` 启动 PWM，用 `__HAL_TIM_SET_COMPARE()` 更新 `CCR1`，用 `HAL_Delay(25)` 控制每次更新之间的间隔。

完整主线链路是：

1. 系统时钟配置到 72MHz。
2. PA0 配成复用推挽输出。
3. TIM2 配成 1kHz PWM：`PSC = 72 - 1`，`ARR = 1000 - 1`。
4. CH1 使用 PWM mode 1。
5. 初始 `CCR1 = 0`，LED 从暗开始。
6. 打开 `OC1PE`，让运行中更新比较值更规整。
7. 触发 `EGR.UG`，让初始化配置装载。
8. 打开 `CC1E` 并启动 `CEN`。
9. 主循环把当前 `duty` 写入 `CCR1`。
10. 延时一小段时间，让人眼看到当前亮度。
11. 到达 0 或 1000 时改变 `direction`。
12. `next_duty()` 根据当前区间算出下一档 duty。

## 6. 核心名词解释

### 6.1 呼吸灯是什么

呼吸灯是 LED 亮度按较慢节奏周期性增强、减弱的视觉效果。

它属于现象层，不是 STM32 里的某个专用硬件模块。硬件只负责输出 PWM，软件负责让占空比按某种规律变化。

本课里呼吸灯由两部分组成：

```text
TIM2_CH1 输出 1kHz PWM
主循环不断改变 CCR1
```

如果 PWM 没有稳定输出，呼吸效果没有基础。如果 `CCR1` 不变化，LED 只会停在某个固定亮度。

### 6.2 `PWM` 在本课里负责什么

PWM 负责把一个数字占空比转换成 LED 平均亮度。

它属于定时器输出层。本课 PWM 频率仍然是 1kHz，和上一课基础 PWM 一样。1kHz 足够快，人眼主要感受到平均亮度。

本课不通过改变 PWM 频率做呼吸，也不通过延时翻转 GPIO 做呼吸。真正变化的是 `CCR1`。

如果把 PWM 频率降得太低，LED 可能闪烁；如果频率保持 1kHz，只改占空比，视觉上更平滑。

### 6.3 `CCR1` 是什么

`CCR1` 是 Capture/Compare Register 1，中文叫捕获/比较寄存器 1。

它属于 TIM2_CH1 通道比较值层。在 PWM mode 1 中，它决定一个 PWM 周期内有效电平持续多久。

本课初始化：

```c
TIM2->CCR1 = 0U;
```

运行中：

```c
TIM2->CCR1 = duty;
```

`ARR + 1 = 1000`，所以 `duty` 在 0 到 1000 之间变化，就近似对应 0% 到 100% 占空比。

### 6.4 `duty` 是什么

`duty` 是软件变量，表示准备写入 `CCR1` 的占空比数值。

它属于 C 代码策略层，不是硬件寄存器。只有执行 `TIM2->CCR1 = duty` 或 `__HAL_TIM_SET_COMPARE(..., duty)` 后，它才影响硬件输出。

本课 `duty` 从 0 开始，逐步增加到 1000，再逐步减小到 0。

如果 `duty` 计算对了但没有写入 `CCR1`，LED 亮度不会变化。

### 6.5 `direction` 是什么

`direction` 是软件变量，中文可以理解为亮度变化方向。

它属于 C 代码控制层。本课中 `direction = 1` 表示下一步变亮，`direction = -1` 表示下一步变暗。

代码逻辑是：

```c
if (duty >= 1000U) {
    direction = -1;
} else if (duty == 0U) {
    direction = 1;
}
```

如果没有方向变量，亮度到达最亮或最暗后就不知道下一步该往哪边走，呼吸循环就不完整。

### 6.6 `next_duty()` 是什么

`next_duty()` 是本课的软件占空比生成函数。

它属于算法策略层，不属于 STM32 硬件。它根据当前 `duty` 和 `direction` 算出下一次要写入 `CCR1` 的值。

寄存器版声明：

```c
static uint16_t next_duty(uint16_t duty, int8_t direction)
```

HAL 版声明：

```c
static uint32_t next_duty(uint32_t duty, int8_t direction)
```

两者返回类型不同，但算法思想相同。它让暗部步进更细，中间区域变化更快，高亮区域再调整步进。

### 6.7 分段步进是什么

分段步进就是不同亮度区间使用不同的 `step`。

它属于视觉效果策略层。本课代码：

```c
if (duty < 120U) {
    step = 5U;
} else if (duty < 400U) {
    step = 15U;
} else if (duty < 750U) {
    step = 25U;
} else {
    step = 12U;
}
```

暗部用 5，变化更细，避免刚亮起来时突兀。中间区域用 15 和 25，让整体变化不拖。高亮区用 12，让接近最亮时不至于一下冲到顶。

如果所有区间都用固定 50，亮度台阶会更明显，呼吸效果会更硬。

### 6.8 `PSC` 是什么

`PSC` 是 Prescaler，中文叫预分频寄存器。

它属于 TIM2 时基层。本课沿用：

```c
TIM2->PSC = 72U - 1U;
```

按 72MHz 输入计算，TIM2 计数频率变成 1MHz。

本课呼吸节奏不是靠改 `PSC` 实现的。`PSC` 保持 PWM 频率稳定。

### 6.9 `ARR` 是什么

`ARR` 是 Auto-Reload Register，中文叫自动重装载寄存器。

它属于 PWM 周期层。本课沿用：

```c
TIM2->ARR = 1000U - 1U;
```

`ARR = 999`，1MHz 计数下周期是 1ms，PWM 频率是 1kHz。

呼吸灯变亮变暗时，`ARR` 不变。改变 `ARR` 会改变 PWM 频率，不是本课的亮度策略。

### 6.10 `OC1M = PWM1` 是什么

`OC1M` 是输出比较 1 模式字段。`PWM1` 是 PWM mode 1。

它属于 TIM2_CH1 输出规则层。本课设置：

```c
TIM2->CCMR1 |= TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2;
```

这让 CH1 按 PWM mode 1 输出。向上计数、有效高时，可以理解为 `CNT < CCR1` 输出有效电平，超过比较值后输出无效电平。

如果这里配置成 toggle，就不是 PWM 呼吸灯，而是比较匹配翻转。

### 6.11 `OC1PE` 是什么

`OC1PE` 是 Output Compare 1 Preload Enable，中文叫输出比较 1 预装载使能。

它属于 PWM 更新同步层。本课主循环不断改 `CCR1`，打开 `OC1PE` 可以让新比较值在更新事件时同步进入实际比较逻辑。

代码：

```c
TIM2->CCMR1 |= TIM_CCMR1_OC1PE;
```

如果不使用预装载，运行中写 `CCR1` 可能在当前周期中间立即影响比较结果，偶尔产生不规整边沿。

### 6.12 `EGR.UG` 是什么

`EGR.UG` 是软件产生更新事件。

它属于定时器事件层。本课代码：

```c
TIM2->EGR |= TIM_EGR_UG;
```

这一步让 `PSC`、`ARR`、`CCR1` 预装载等配置在启动阶段同步。它让定时器从一开始就按当前配置工作。

如果省略，程序通常可能继续运行，但启动瞬间第一个 PWM 周期可能不是你预期的状态。

### 6.13 `CC1E` 是什么

`CC1E` 是通道 1 输出使能位。

它属于 TIM2_CH1 输出开关层。本课代码：

```c
TIM2->CCER |= TIM_CCER_CC1E;
```

它允许 CH1 输出送到复用引脚 PA0。PWM 内部生成和外部引脚输出之间隔着这个开关。

如果没有 `CC1E`，`next_duty()` 算得再漂亮，PA0 也没有 PWM。

### 6.14 `HAL_TIM_PWM_Start()` 是什么

`HAL_TIM_PWM_Start()` 是 HAL 启动 PWM 输出的接口。

它属于 HAL 运行控制层。本课代码：

```c
HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1)
```

它让 TIM2_CH1 开始输出 PWM。底层对应打开通道输出和启动计数器。

如果只调用 `HAL_TIM_PWM_ConfigChannel()`，不调用 Start，PA0 不会输出呼吸灯 PWM。

### 6.15 `__HAL_TIM_SET_COMPARE()` 是什么

`__HAL_TIM_SET_COMPARE()` 是 HAL 修改通道比较值的宏。

它属于运行期占空比更新层。本课代码：

```c
__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, duty);
```

对 TIM2_CH1 来说，它本质上是写 `CCR1`，对应寄存器版：

```c
TIM2->CCR1 = duty;
```

如果通道传错，软件变量仍然在变，但 PA0 对应的 CH1 不会跟着变。

### 6.16 `HAL_Delay(25)` 和 `delay(120000U)` 是什么

它们属于软件节奏层。

寄存器版：

```c
delay(120000U);
```

HAL 版：

```c
HAL_Delay(25);
```

这两者都不生成 PWM，只决定多久更新一次 `duty`。延时越短，呼吸变化越快；延时越长，呼吸变化越慢。

### 6.17 `__HAL_AFIO_REMAP_SWJ_NOJTAG()` 是什么

这是 HAL 中关闭 JTAG、保留 SWD 的 AFIO 重映射宏。

它属于调试引脚复用工程层。本课 HAL 版在 `hal_msp_init_minimal()` 里调用：

```c
__HAL_AFIO_REMAP_SWJ_NOJTAG();
```

BluePill 常用 ST-Link/SWD 下载调试。关闭 JTAG、保留 SWD 可以释放部分被 JTAG 占用的 GPIO。PA0 本身不是 JTAG 引脚，但这是后续更多 GPIO 实验中常见的工程习惯。

### 6.18 `pwm_to_din_test` 是什么

`pwm_to_din_test` 是本课附带的边界观察实验。

它属于实验验证层，不是呼吸灯主线。它让 PA0 输出普通 1kHz PWM，然后接到数字灯条 DIN，观察灯条大概率不亮、乱闪、乱色或偶尔闪一下。

它出现的原因是：很多初学者会把“PWM 控亮度”和“数字灯条 DIN 控颜色”混在一起。普通 PWM 只有周期和占空比，数字灯条 DIN 需要按协议发送一串精确高低电平编码。

## 7. 寄存器版代码逐步讲解

### 7.1 `delay()` 只控制呼吸变化速度

代码：

```c
static void delay(volatile uint32_t count)
{
    while (count--) {
        __NOP();
    }
}
```

它不负责生成 PWM。PWM 由 TIM2 自动输出。这个延时只控制主循环多久更新一次 `CCR1`。

如果延时变短，亮度变化更快；如果延时变长，呼吸周期更慢。

### 7.2 系统时钟仍然配置为 72MHz

`system_clock_72mhz_init()` 打开 HSE，等待 `HSERDY`，配置 PLL 9 倍频，再切换 SYSCLK 到 PLL。

本课 PWM 参数沿用：

```text
72MHz / 72 / 1000 = 1kHz
```

所以系统时钟是后续计算的基础。时钟不对，PWM 频率就不对。

### 7.3 PA0 初始化仍然是复用推挽

代码：

```c
RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN;
GPIOA->CRL &= ~(GPIO_CRL_MODE0 | GPIO_CRL_CNF0);
GPIOA->CRL |= GPIO_CRL_MODE0_1 | GPIO_CRL_CNF0_1;
```

`IOPAEN` 打开 GPIOA，`AFIOEN` 打开 F1 复用功能相关模块。`MODE0 = 10`、`CNF0 = 10` 让 PA0 成为复用推挽输出。

硬件后果是 TIM2_CH1 可以驱动 PA0。

### 7.4 打开 TIM2 时钟

代码：

```c
RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
```

TIM2 在 APB1 上。没有 TIM2 时钟，后面的 `PSC`、`ARR`、`CCR1`、`CCMR1` 配置没有正常运行基础。

### 7.5 配置 1MHz 计数和 1kHz PWM

代码：

```c
TIM2->PSC = 72U - 1U;
TIM2->ARR = 1000U - 1U;
```

`PSC = 71` 表示除以 72，得到 1MHz 计数。`ARR = 999` 表示每 1000 个计数更新一次，得到 1kHz PWM。

这两个参数在呼吸过程中不变。亮度变化不靠改它们。

### 7.6 初始 `CCR1 = 0`

代码：

```c
TIM2->CCR1 = 0U;
```

这表示初始占空比接近 0%，LED 从暗开始。

和上一课初始 25% 不同，本课为了呼吸灯效果，起点放在最暗处。

### 7.7 设置 CH1 为 PWM mode 1

代码：

```c
TIM2->CCMR1 &= ~(TIM_CCMR1_CC1S | TIM_CCMR1_OC1M);
TIM2->CCMR1 |= TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2;
```

第一句清掉通道选择和旧输出模式。`CC1S = 00` 表示通道 1 是输出。第二句设置 `OC1M = 110`，也就是 PWM mode 1。

PWM mode 1 让 `CCR1` 成为占空比控制值。

### 7.8 打开 `OC1PE`

代码：

```c
TIM2->CCMR1 |= TIM_CCMR1_OC1PE;
```

本课会频繁写 `CCR1`。打开预装载后，新值在更新事件同步生效，减少当前周期中间突然改变比较值带来的异常边沿。

呼吸灯虽然肉眼看的是慢变化，但底层 PWM 仍然是高速周期波形，更新同步很重要。

### 7.9 打开输出、更新、启动

代码：

```c
TIM2->CCER |= TIM_CCER_CC1E;
TIM2->EGR |= TIM_EGR_UG;
TIM2->CR1 |= TIM_CR1_CEN;
```

`CC1E` 打开 CH1 输出。`UG` 产生一次更新事件，让配置装载。`CEN` 启动计数器。

这三步完成后，TIM2_CH1 开始在 PA0 输出 PWM。

### 7.10 `duty` 和 `direction` 的初始值

代码：

```c
uint16_t duty = 0U;
int8_t direction = 1;
```

`duty = 0` 表示从最暗开始。`direction = 1` 表示先变亮。

这两个变量属于软件节奏控制，不是硬件寄存器。

### 7.11 主循环先写当前占空比

代码：

```c
TIM2->CCR1 = duty;
```

这一步把软件算出的亮度值写入硬件比较寄存器。写完后，TIM2_CH1 的 PWM 占空比会按更新机制变化。

如果只更新 `duty` 变量但不写 `CCR1`，LED 不会跟着变。

### 7.12 边界处改变方向

代码：

```c
if (duty >= 1000U) {
    direction = -1;
} else if (duty == 0U) {
    direction = 1;
}
```

到 1000 后开始变暗，到 0 后开始变亮。这样形成循环。

这里用 1000 是因为 `ARR + 1 = 1000`，它代表接近 100% 占空比的上界。

### 7.13 `next_duty()` 选择分段步长

代码：

```c
if (duty < 120U) {
    step = 5U;
} else if (duty < 400U) {
    step = 15U;
} else if (duty < 750U) {
    step = 25U;
} else {
    step = 12U;
}
```

这段不是硬件要求，而是视觉策略。不同区间步长不同，呼吸节奏比固定步进更自然。

### 7.14 `next_duty()` 处理变亮

代码：

```c
if (direction > 0) {
    if ((uint32_t)duty + step >= 1000U) {
        return 1000U;
    }
    return (uint16_t)(duty + step);
}
```

方向为正时增加 duty。如果下一步超过 1000，就钳到 1000，避免超过 PWM 周期上限。

### 7.15 `next_duty()` 处理变暗

代码：

```c
if (duty <= step) {
    return 0U;
}
return (uint16_t)(duty - step);
```

方向为负时减少 duty。如果下一步会低于 0，就钳到 0。

这能避免无符号整数下溢。否则 `uint16_t` 从 0 再减会绕回很大的值，LED 亮度会突然跳变。

## 8. HAL 版代码逐步讲解

### 8.1 `HAL_Init()` 建立 HAL 基础状态

代码：

```c
HAL_Init();
```

HAL 初始化后，`HAL_Delay()` 和 HAL 内部状态才有基础。它不是 PWM 输出本身，但影响主循环更新节奏。

### 8.2 `hal_msp_init_minimal()` 打开 AFIO/PWR 并关闭 JTAG

代码：

```c
__HAL_RCC_AFIO_CLK_ENABLE();
__HAL_RCC_PWR_CLK_ENABLE();
__HAL_AFIO_REMAP_SWJ_NOJTAG();
```

AFIO 时钟用于 F1 复用和重映射相关功能。PWR 是 HAL 工程常见基础时钟。`NOJTAG` 关闭 JTAG、保留 SWD，避免 JTAG 占住部分 GPIO。

本课 PA0 不依赖关闭 JTAG，但这是 BluePill 后续多引脚实验中很实用的工程设置。

### 8.3 HAL 时钟配置目标仍是 72MHz

`RCC_OscInitTypeDef osc` 配置 HSE 和 PLL，`RCC_ClkInitTypeDef clk` 配置 SYSCLK、HCLK、PCLK1、PCLK2。

这些字段最终写入 RCC/FLASH 寄存器。目标和寄存器版一致：让后面的 `Prescaler = 72 - 1` 能得到 1MHz 计数。

### 8.4 GPIOA PA0 配成复用推挽

代码：

```c
gpio.Pin = GPIO_PIN_0;
gpio.Mode = GPIO_MODE_AF_PP;
gpio.Pull = GPIO_NOPULL;
gpio.Speed = GPIO_SPEED_FREQ_LOW;
HAL_GPIO_Init(GPIOA, &gpio);
```

`GPIO_MODE_AF_PP` 对应 PA0 复用推挽输出。它让 PA0 接收 TIM2_CH1 的 PWM，而不是普通 GPIO 输出。

### 8.5 `htim2.Instance = TIM2`

代码：

```c
htim2.Instance = TIM2;
```

这选择硬件定时器实例。它对应寄存器版所有 `TIM2->...`。

如果实例不是 TIM2，就算 HAL 调用都成功，PA0 也不一定有 TIM2_CH1 输出。

### 8.6 `Prescaler` 和 `Period`

代码：

```c
htim2.Init.Prescaler = 72U - 1U;
htim2.Init.Period = 1000U - 1U;
```

`Prescaler` 对应 `PSC`，`Period` 对应 `ARR`。

它们共同决定 1kHz PWM。呼吸效果运行时不改这两个字段。

### 8.7 `HAL_TIM_PWM_Init()` 初始化 PWM 时基

代码：

```c
if (HAL_TIM_PWM_Init(&htim2) != HAL_OK) {
    error_handler();
}
```

它把 `htim2.Init` 里的时基配置写入 TIM2。失败时进入 `error_handler()`，避免带着错误外设状态继续执行。

### 8.8 `sConfigOC.Pulse = 0`

代码：

```c
sConfigOC.Pulse = 0U;
```

这对应初始 `CCR1 = 0`。LED 从最暗开始。

后续主循环会用 `__HAL_TIM_SET_COMPARE()` 覆盖它。

### 8.9 `OCMode`、`OCPolarity`、`OCFastMode`

代码：

```c
sConfigOC.OCMode = TIM_OCMODE_PWM1;
sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
```

`TIM_OCMODE_PWM1` 对应 `OC1M = 110`。`TIM_OCPOLARITY_HIGH` 表示有效电平为高。`TIM_OCFAST_DISABLE` 表示不使用快速模式。

这些字段共同定义 TIM2_CH1 如何把 `CCR1` 变成 PA0 上的波形。

### 8.10 `HAL_TIM_PWM_ConfigChannel()` 配置 CH1

代码：

```c
HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1)
```

这把 `sConfigOC` 写入 TIM2_CH1 的通道配置。`TIM_CHANNEL_1` 对应 PA0 默认映射。

如果误用其他通道，`__HAL_TIM_SET_COMPARE()` 也跟着改错通道，PA0 不会按预期呼吸。

### 8.11 `HAL_TIM_PWM_Start()` 启动 PWM

代码：

```c
HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1)
```

它打开通道输出并启动计数器。配置完成但没有 Start，PA0 不会有运行中的 PWM。

### 8.12 HAL 主循环更新比较值

代码：

```c
__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, duty);
HAL_Delay(25);
```

第一句把 `duty` 写入 TIM2_CH1 比较值。第二句让当前亮度保持一小段时间。

这里的 `HAL_Delay(25)` 控制呼吸变化速度，不控制 PWM 频率。

### 8.13 HAL 版 `next_duty()`

HAL 版：

```c
static uint32_t next_duty(uint32_t duty, int8_t direction)
```

算法和寄存器版一样，只是使用 `uint32_t`。它同样按 120、400、750 三个边界选择不同步进。

### 8.14 `SysTick_Handler()` 支撑 `HAL_Delay()`

代码：

```c
void SysTick_Handler(void)
{
    HAL_IncTick();
}
```

`HAL_Delay()` 依赖 HAL tick 递增。最小工程里如果缺少这个中断函数，程序可能卡在默认中断或延时不返回。

PWM 硬件本身不靠 SysTick 输出，但占空比更新节奏靠它。

### 8.15 `error_handler()` 的工程意义

代码：

```c
static void error_handler(void)
{
    __disable_irq();
    while (1) {
    }
}
```

当时钟或 PWM 初始化失败时，程序停在这里，避免继续运行未知配置。

如果 HAL 版没有现象，可以用调试器看是否进入了 `error_handler()`。

## 9. 两个版本真正应该怎么学

寄存器版要分两层看。

第一层是硬件 PWM 层：

```text
PA0 复用推挽
TIM2 1kHz 时基
CH1 PWM mode 1
CCR1 控制占空比
CC1E/CEN 启动输出
```

第二层是软件呼吸策略层：

```text
duty 保存当前亮度
direction 保存变化方向
next_duty() 生成下一档亮度
delay 控制每档停留时间
```

HAL 版也一样。`HAL_TIM_PWM_Init()`、`HAL_TIM_PWM_ConfigChannel()`、`HAL_TIM_PWM_Start()` 是硬件 PWM 层；`__HAL_TIM_SET_COMPARE()`、`next_duty()`、`HAL_Delay()` 是运行时亮度策略层。

本课不要把呼吸灯想成新的定时器模式。它仍然是 PWM，只是软件用更精细的方式改占空比。

## 10. 检验问题清单

### 10.1 为什么本课不需要改变 `ARR` 就能改变亮度？

因为亮度由占空比决定，占空比由 `CCR1` 相对 `ARR + 1` 的比例决定。`ARR` 保持不变可以保持 PWM 频率稳定，只改 `CCR1` 就能改高电平宽度。

### 10.2 `next_duty()` 是硬件功能吗？

不是。它是 C 代码里的软件函数，只负责计算下一次写入 `CCR1` 的值。TIM2 并不知道“呼吸”这个概念。

### 10.3 为什么低亮度区步进用 5？

暗部刚亮起来时，人眼对变化容易感觉突兀。用较小步进可以让起亮更细，视觉上更柔和。

### 10.4 `direction` 为什么要在 0 和 1000 处改变？

0 接近最暗，1000 接近最亮。到边界后反向，才能形成暗到亮、亮到暗的循环。

### 10.5 `OC1PE` 打开后有什么好处？

运行中写 `CCR1` 时，新比较值按更新事件同步进入实际比较逻辑，减少当前 PWM 周期中间突然变化导致的不规整边沿。

### 10.6 HAL 版 `__HAL_TIM_SET_COMPARE()` 对应寄存器版哪句？

对应 `TIM2->CCR1 = duty;`。本课使用 `TIM_CHANNEL_1`，所以改的是 TIM2_CH1 的比较值。

### 10.7 `HAL_Delay(25)` 会改变 PWM 频率吗？

不会。它只改变占空比更新间隔，也就是呼吸变化速度。PWM 频率由 `Prescaler` 和 `Period` 决定。

### 10.8 普通 PWM 接灯条 DIN 为什么不能正常控制颜色？

因为数字灯条 DIN 需要按协议发送精确编码的 0/1 数据流。普通 1kHz PWM 只有周期和占空比，不包含颜色数据帧、复位时序和每一位的高低电平宽度编码。

## 11. 工程实现步骤

### 11.1 需求分析

本课主需求是：PA0 输出稳定 1kHz PWM，同时 LED 亮度按呼吸节奏变化。

拆开就是：

- TIM2_CH1 生成 PWM。
- PA0 正确输出 TIM2_CH1。
- `CCR1` 可运行中更新。
- 软件有亮度方向。
- 软件有下一档占空比算法。
- 占空比变化速度适合人眼观察。

### 11.2 硬件核查

主实验检查：

- PA0 是否接到 LED。
- LED 是否串联限流电阻。
- LED 负极是否接 GND。
- STM32 是否正常供电。
- 如果用示波器，探头地是否和开发板共地。

`pwm_to_din_test` 检查：

- 灯条使用外部 5V 电源。
- 灯条 GND 和 STM32 GND 共地。
- PA0 到 DIN 串 220Ω 到 470Ω 电阻。
- 预期不是正常显示颜色，而是观察普通 PWM 对 DIN 不成立。

### 11.3 寄存器路线

寄存器版实现顺序：

1. 配置系统时钟到 72MHz。
2. 打开 GPIOA 和 AFIO 时钟。
3. PA0 配置为复用推挽输出。
4. 打开 TIM2 时钟。
5. 设置 `PSC = 72 - 1`。
6. 设置 `ARR = 1000 - 1`。
7. 设置初始 `CCR1 = 0`。
8. 设置 CH1 输出模式和 PWM mode 1。
9. 打开 `OC1PE`。
10. 打开 `CC1E`。
11. 触发 `EGR.UG`。
12. 设置 `CR1.CEN`。
13. 主循环写 `TIM2->CCR1 = duty`。
14. 根据边界更新 `direction`。
15. 调用 `next_duty()` 计算下一档。

### 11.4 HAL 路线

HAL 版实现顺序：

1. `HAL_Init()`。
2. `hal_msp_init_minimal()` 打开 AFIO/PWR，关闭 JTAG 保留 SWD。
3. 配置系统时钟。
4. 配置 PA0 为 `GPIO_MODE_AF_PP`。
5. 打开 TIM2 时钟。
6. `htim2.Instance = TIM2`。
7. 设置 `Prescaler = 72 - 1`。
8. 设置 `Period = 1000 - 1`。
9. 调用 `HAL_TIM_PWM_Init()`。
10. 设置 `TIM_OC_InitTypeDef`，其中 `Pulse = 0`。
11. 调用 `HAL_TIM_PWM_ConfigChannel()`。
12. 调用 `HAL_TIM_PWM_Start()`。
13. 主循环调用 `__HAL_TIM_SET_COMPARE()`。
14. 用 `next_duty()` 生成下一档。

### 11.5 工程思维

本课的核心工程思维是分层：硬件输出高频 PWM，软件低频更新参数。

高频部分交给 TIM2，因为 PWM 周期必须稳定。低频亮度策略交给主循环，因为呼吸节奏不需要微秒级精度，而且后续容易改算法。

这比用软件延时直接拉高拉低 PA0 更可靠，也更容易扩展。例如以后可以把 `next_duty()` 换成查表、正弦曲线、按键调速，而 TIM2 PWM 底层不需要改。

### 11.6 常见工程陷阱

第一个陷阱是把呼吸灯当成新的硬件模式。实际上硬件仍然是普通 PWM。

第二个陷阱是改 `ARR` 做亮度变化。这样会改变 PWM 频率，可能带来闪烁。亮度应优先改 `CCR1`。

第三个陷阱是 `duty` 越界。超过 `ARR + 1` 后，占空比不再按直觉变化。

第四个陷阱是无符号减法下溢。本课 `next_duty()` 在 `duty <= step` 时返回 0，就是为了避免从 0 减成很大的数。

第五个陷阱是把普通 PWM 当数字灯条协议。灯条 DIN 需要特定数据时序，不是随便给一个占空比波形就能显示颜色。

## 12. 运行现象

主实验下载后，PA0 外接普通 LED 会从暗到亮，再从亮到暗，循环变化。

用示波器观察 PA0：

- PWM 频率约 1kHz。
- 周期约 1ms。
- 占空比从接近 0% 逐步增大到接近 100%。
- 到达上限后，占空比再逐步减小。

寄存器版和 HAL 版的呼吸速度可能略有差异，因为一个用空循环延时，一个用 `HAL_Delay(25)`，但硬件 PWM 原理一致。

`pwm_to_din_test` 下载后，如果把 PA0 接到数字灯条 DIN，常见现象是不亮、乱闪、乱色或偶尔闪一下。这是预期现象，说明普通 PWM 不是灯条数据协议。

## 13. 常见问题排查

### 13.1 LED 完全不亮

先查接线：PA0、限流电阻、LED 极性、GND。

再查 PWM 输出链路：GPIOA 时钟、PA0 复用推挽、TIM2 时钟、`OC1M`、`CC1E`、`CEN`。

如果示波器有 PWM 但 LED 不亮，多半是 LED 方向或接线问题。

### 13.2 LED 有亮度但不呼吸

检查主循环是否写入 `CCR1`。

寄存器版看：

```c
TIM2->CCR1 = duty;
```

HAL 版看：

```c
__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, duty);
```

如果这一步没执行或通道选错，PWM 会停在初始占空比。

### 13.3 呼吸到最亮或最暗后卡住

检查 `direction` 的边界判断是否正确。

到 `duty >= 1000` 应该改为 `-1`，到 `duty == 0` 应该改为 `1`。如果边界没有反向，亮度会停在一端。

### 13.4 亮度突然跳变

检查 `next_duty()` 的减法边界。

如果 `uint16_t duty` 在 0 附近继续减，可能发生无符号下溢，数值绕回很大，LED 会突然跳亮。本课用 `if (duty <= step) return 0U;` 避免这个问题。

### 13.5 波形频率不对

重新检查：

```text
TIM2 计数频率 = 72MHz / (PSC + 1)
PWM 频率 = TIM2 计数频率 / (ARR + 1)
```

确认系统时钟、`PSC`、`ARR` 和课程计算一致。

### 13.6 HAL 版卡住或不更新

用调试器看是否进入 `error_handler()`。如果是，检查 HSE、时钟配置、TIM2 初始化返回值。

如果卡在 `HAL_Delay()`，检查 `SysTick_Handler()` 是否存在并调用 `HAL_IncTick()`。

### 13.7 数字灯条 DIN 乱闪

这是 `pwm_to_din_test` 的预期观察之一。

普通 PWM 不包含灯条需要的数据帧。若要控制 WS2812/SK6812，要使用符合协议的定时编码、SPI/DMA 技巧或专门的驱动方式，而不是 1kHz 亮度 PWM。

## 14. 本课最核心的结论

呼吸灯不是新的 STM32 外设功能，而是：

```text
稳定 PWM 输出 + 周期性更新 CCR1 + 合理的占空比变化策略
```

TIM2 负责严格的 1kHz PWM，主循环负责较慢的亮度节奏。`next_duty()` 决定观感，`CCR1` 决定硬件占空比，PA0 负责把 TIM2_CH1 输出到 LED。

## 15. 建议你现在怎么读这节课

先把它和上一课对比：PWM 初始化几乎一样，真正变化集中在主循环和 `next_duty()`。

然后按两条线读：

- 硬件线：PA0、TIM2_CH1、`PSC`、`ARR`、`CCR1`、PWM mode 1。
- 软件线：`duty`、`direction`、分段 `step`、边界反向、延时。

最后看 `pwm_to_din_test`，记住普通 PWM 和数字灯条协议的边界。

## 16. 扩展练习

1. 把 `next_duty()` 改成固定步进 20，对比呼吸观感。
2. 把最高占空比限制在 800，观察最大亮度变化。
3. 把低亮度区步进从 5 改成 1，观察起亮是否更细。
4. 把 HAL 版 `HAL_Delay(25)` 改成 10 或 60，观察呼吸速度。
5. 用示波器验证呼吸过程中 PWM 频率不变，只有占空比变化。
6. 故意去掉 `OC1PE`，观察波形在更新时是否更容易出现不规整。
7. 运行 `pwm_to_din_test`，观察普通 PWM 接 DIN 和真正灯条协议的区别。

## 17. 下一课预告

上一课：[08_pwm_basic](../08_pwm_basic/README.md)

下一课：[10_exti](../10_exti/README.md)

下一课会从定时器 PWM 转到外部中断。重点会变成：外部引脚电平变化如何通过 EXTI 线、NVIC 和中断服务函数进入软件处理。

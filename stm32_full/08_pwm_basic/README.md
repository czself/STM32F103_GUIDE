# 08_pwm_basic - PWM 基础

## 1. 本课到底在学什么

本课表面现象是：PA0 输出 1kHz PWM，外接 LED 的亮度会从暗到亮、再从亮到暗逐级变化。

真正要学的是 PWM 的硬件生成链路。PWM 不是主循环用延时反复拉高拉低 PA0，而是 TIM2_CH1 在每个计数周期里自动比较 `CNT` 和 `CCR1`，再按照 PWM mode 1 的规则决定输出高电平还是低电平。

本课和上一课 `07_timer_output_compare` 是连续的。上一课是 `CNT == CCR1` 时翻转一次输出；本课是 `CNT` 在一个周期里小于 `CCR1` 时保持有效电平，超过比较值后变为无效电平。也就是说，输出比较从“匹配点触发一个动作”变成了“比较值决定整个周期内高低电平比例”。

注意：本课不是固定 25% 占空比演示。初始化时 `CCR1 = 250`，但主循环会不断把 `CCR1` 改成 `duty`。所以你看到的是 LED 亮度阶梯式变化。

## 2. 本课学习目标

学完本课，你应该能做到：

- 根据 `PSC = 72 - 1` 和 `ARR = 1000 - 1` 算出 PWM 频率是 1kHz。
- 根据 `CCR1 / (ARR + 1)` 估算 PWM 占空比。
- 解释 PWM mode 1 中 `CNT < CCR1` 和 `CNT >= CCR1` 时输出状态为什么不同。
- 说明为什么 PA0 必须配置成复用推挽输出。
- 解释 `OC1PE` 为什么适合运行中修改 `CCR1`。
- 看懂寄存器版 `TIM2->CCR1 = duty` 为什么会改变 LED 平均亮度。
- 看懂 HAL 版 `__HAL_TIM_SET_COMPARE()` 为什么等价于修改指定通道的比较寄存器。
- 区分 PWM 频率、占空比、亮度变化速度三件事。

## 3. 本课目录结构

```text
08_pwm_basic/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

`reg/` 直接写 TIM2 和 GPIOA 的寄存器。重点看 `PSC`、`ARR`、`CCR1`、`CCMR1.OC1M`、`OC1PE`、`CCER.CC1E`。

`hal/` 使用 HAL PWM 接口。重点看 `TIM_HandleTypeDef`、`TIM_OC_InitTypeDef`、`HAL_TIM_PWM_Init()`、`HAL_TIM_PWM_ConfigChannel()`、`HAL_TIM_PWM_Start()`、`__HAL_TIM_SET_COMPARE()`。

两份代码做的是同一件事：TIM2_CH1 通过 PA0 输出 1kHz PWM，并在运行中修改占空比。

## 4. 实验硬件

本课使用：

- STM32F103C8T6 BluePill
- ST-Link 下载器
- PA0 引脚
- 220Ω 左右限流电阻
- LED
- 可选：示波器或逻辑分析仪

推荐接法：

```text
PA0 -> 限流电阻 -> LED 正极
LED 负极 -> GND
```

这种接法下，PA0 输出高电平时 LED 亮，低电平时 LED 灭。PWM 频率是 1kHz，人眼看不到每一次快速闪烁，只会感受到平均亮度。占空比越大，一个周期内高电平时间越长，LED 平均亮度越高。

如果你把 LED 接到 VCC，再由 PA0 下拉点亮，那么亮度直觉会反过来：PA0 低电平时间越长，LED 越亮。排查亮度和占空比关系时一定要先看接法。

## 5. 先建立一个最基本的脑图

本课可以拆成六层来看。

现象层：PA0 外接 LED 亮度逐级变化。如果用示波器看，PA0 是周期约 1ms 的 PWM，周期基本固定，占空比不断改变。

物理/硬件层：PA0 是真实引脚。它必须被配置为复用推挽输出，这样 TIM2_CH1 的输出信号才能驱动引脚。LED 看到的是 PA0 电平的快速高低变化，人眼看到的是平均亮度。

芯片模块层：RCC 给 GPIOA、AFIO、TIM2 送时钟；GPIOA 配置 PA0 输出模式；TIM2 生成 PWM；TIM2_CH1 是具体输出通道。

寄存器/bit 层：`PSC` 决定计数频率，`ARR` 决定 PWM 周期，`CCR1` 决定高电平宽度，`CCMR1.OC1M` 选择 PWM mode 1，`OC1PE` 让比较值预装载，`CCER.CC1E` 打开通道输出，`CR1.CEN` 启动计数器。

C/CMSIS 层：寄存器版通过 `TIM2->PSC`、`TIM2->ARR`、`TIM2->CCR1` 直接写硬件，通过 `TIM_CCMR1_OC1M_1`、`TIM_CCMR1_OC1M_2` 这些宏设置 bit。

HAL/工程层：HAL 版用结构体字段表达同样配置，用 `HAL_TIM_PWM_Start()` 启动 PWM，用 `__HAL_TIM_SET_COMPARE()` 在主循环中更新占空比。

完整链路是：

1. 系统时钟配置到 72MHz。
2. 打开 GPIOA、AFIO、TIM2 时钟。
3. PA0 配置成复用推挽输出。
4. `PSC = 72 - 1`，TIM2 计数频率变为 1MHz。
5. `ARR = 1000 - 1`，每 1000 个计数形成一个 PWM 周期，也就是 1kHz。
6. `CCR1 = 250`，初始高电平宽度约 250 个计数，占空比约 25%。
7. `OC1M = 110`，选择 PWM mode 1。
8. `OC1PE = 1`，运行中修改 `CCR1` 时使用预装载。
9. `CC1E = 1`，TIM2_CH1 输出打开。
10. `CEN = 1`，TIM2 开始计数。
11. 主循环不断写 `CCR1`，占空比随 `duty` 从 0 到 1000 再回到 0。

## 6. 核心名词解释

### 6.1 `PWM` 是什么

`PWM` 是 Pulse Width Modulation，中文叫脉宽调制。

它属于外设输出波形层。PWM 的频率通常保持固定，通过改变一个周期内高电平所占比例来改变平均输出效果。

本课里 PWM 作用在 LED 上。LED 实际在 1kHz 下快速亮灭，人眼来不及分辨每个周期，只感觉平均亮度变化。

如果 PWM 频率太低，LED 会肉眼可见闪烁。如果占空比计算错，亮度变化会不符合预期。

### 6.2 PWM 频率是什么

PWM 频率就是 PWM 周期重复的速度。

它属于时间参数层，由 TIM2 计数频率和 `ARR` 共同决定：

```text
PWM 频率 = TIM2 计数频率 / (ARR + 1)
```

本课 `PSC = 72 - 1`，TIM2 计数频率是 1MHz；`ARR = 1000 - 1`，所以 PWM 频率是 1MHz / 1000 = 1kHz。

如果你想改 PWM 频率，主要改 `PSC` 或 `ARR`，不是改主循环延时。

### 6.3 占空比是什么

占空比就是一个 PWM 周期内有效电平所占比例。

它属于波形形态层。本课使用 PWM mode 1、有效极性为高，可以近似理解为：

```text
占空比 = CCR1 / (ARR + 1)
```

`ARR + 1 = 1000` 时，`CCR1 = 250` 约为 25%，`CCR1 = 500` 约为 50%，`CCR1 = 900` 约为 90%。

如果 LED 接法是 PA0 高电平点亮，占空比越大越亮。如果 LED 接法反相，亮度直觉也会反相。

### 6.4 `TIM2_CH1` 是什么

`TIM2_CH1` 是 TIM2 的通道 1。

它属于定时器通道层。PWM 不是整个 TIM2 随便输出，而是由某个通道输出。本课选择 CH1，因为它默认映射到 PA0。

代码中通道 1 对应 `CCR1`、`OC1M`、`OC1PE`、`CC1E` 这些字段。HAL 中对应 `TIM_CHANNEL_1`。

如果 HAL 里误用 `TIM_CHANNEL_2`，你可能配置了 TIM2 的另一个通道，但 PA0 不会输出本课预期的 PWM。

### 6.5 `PA0` 是什么

`PA0` 是 GPIOA 的第 0 号引脚。

它属于物理引脚层和 GPIO 外设层。TIM2_CH1 的 PWM 要从芯片出来，必须通过某个引脚。本课使用默认映射 PA0。

寄存器版通过 `GPIOA->CRL` 配置 PA0，HAL 版通过 `HAL_GPIO_Init(GPIOA, &gpio)` 配置 PA0。

如果 PA0 没有配置为复用推挽，TIM2_CH1 即使内部产生了 PWM，外部 LED 也看不到正确波形。

### 6.6 复用推挽输出是什么

复用推挽输出表示引脚输出来源来自片上外设，输出驱动方式是推挽。

它属于 GPIO 模式层。STM32F1 中，PA0 的 `MODE0` 和 `CNF0` 位于 `GPIOA->CRL`。本课设置 `MODE0 = 10`、`CNF0 = 10`，表示 2MHz 复用推挽输出。

本课必须使用它，因为 PA0 的电平由 TIM2_CH1 PWM 控制，不由普通 `GPIOA->ODR` 控制。

如果写成普通推挽输出，你可能能手动拉高拉低 PA0，但不会得到 TIM2_CH1 自动 PWM。

### 6.7 `PSC` 是什么

`PSC` 是 Prescaler，中文叫预分频寄存器。

它属于 TIM2 时基层。它把定时器输入时钟分频后再送给 `CNT`。

本课代码：

```c
TIM2->PSC = 72U - 1U;
```

实际分频系数是 `PSC + 1 = 72`。按 TIM2 输入 72MHz 计算，`CNT` 频率变成 1MHz。

如果 `PSC` 写成 `720 - 1`，PWM 频率会变成 100Hz，LED 可能出现更明显闪烁。

### 6.8 `ARR` 是什么

`ARR` 是 Auto-Reload Register，中文叫自动重装载寄存器。

它属于 TIM2 周期控制层。向上计数时，`CNT` 从 0 数到 `ARR`，然后更新并回到 0。

本课代码：

```c
TIM2->ARR = 1000U - 1U;
```

`ARR = 999` 表示一轮 1000 个计数。1MHz 计数频率下，一轮时间是 1ms，也就是 PWM 频率 1kHz。

如果 `ARR` 改小，PWM 频率变高，但占空比分辨率降低。如果 `ARR` 改大，PWM 频率变低，但可调档位更多。

### 6.9 `CNT` 是什么

`CNT` 是 Counter，中文叫计数器当前值。

它属于 TIM2 内部计数层。本课代码没有直接写 `CNT`，但 PWM 的每一个高低电平判断都依赖它。

在 PWM mode 1 中，可以把一轮理解成：`CNT` 从 0 开始走，低于 `CCR1` 时输出有效电平，到达比较值后输出状态改变，直到更新事件进入下一周期。

如果 `CR1.CEN` 没有启动，`CNT` 不跑，PWM 就没有周期。

### 6.10 `CCR1` 是什么

`CCR1` 是 Capture/Compare Register 1，中文叫捕获/比较寄存器 1。

它属于 TIM2_CH1 通道比较值层。在 PWM 中，它决定一个周期内有效电平持续多久。

本课初始化：

```c
TIM2->CCR1 = 250U;
```

主循环不断执行：

```c
TIM2->CCR1 = duty;
```

`ARR + 1 = 1000`，所以 `CCR1 = duty` 时，`duty` 大约就是千分比占空比。`duty = 0` 接近 0%，`duty = 500` 接近 50%，`duty = 1000` 接近 100%。

### 6.11 `OC1M` 是什么

`OC1M` 是 Output Compare 1 Mode，中文叫输出比较 1 模式字段。

它属于 `TIM2->CCMR1` 的 bit 字段。它决定 TIM2_CH1 如何根据 `CNT` 和 `CCR1` 产生输出。

本课设置：

```c
TIM2->CCMR1 |= TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2;
```

这让 `OC1M = 110`，对应 PWM mode 1。

如果 `OC1M` 仍然是上一课的 toggle，PA0 会变成匹配翻转，而不是周期性 PWM 占空比输出。

### 6.12 PWM mode 1 是什么

PWM mode 1 是定时器输出比较的一种模式。

它属于通道输出规则层。在向上计数且有效极性为高时，可以简单理解为：`CNT < CCR1` 时输出有效电平，`CNT >= CCR1` 后输出无效电平。

本课用它把 `CCR1` 转换成高电平宽度。`CCR1` 越大，一个周期内高电平持续越久，LED 平均亮度越高。

如果换成 PWM mode 2，高低关系会反过来，亮度变化直觉也会变化。

### 6.13 `OC1PE` 是什么

`OC1PE` 是 Output Compare 1 Preload Enable，中文叫输出比较 1 预装载使能。

它属于通道比较值更新机制层。打开后，运行中写入的 `CCR1` 新值不会随意打断当前周期，而是在更新事件后进入实际比较逻辑。

本课代码：

```c
TIM2->CCMR1 |= TIM_CCMR1_OC1PE;
```

本课主循环不断改 `CCR1`，打开预装载能让占空比更新更规整，减少在周期中间突然改比较值导致的波形毛刺风险。

### 6.14 `CC1E` 是什么

`CC1E` 是 Capture/Compare 1 Output Enable，中文叫通道 1 输出使能。

它属于通道输出开关层。本课代码：

```c
TIM2->CCER |= TIM_CCER_CC1E;
```

PWM 模式配置好了，不代表外部引脚一定有波形。`CC1E` 打开后，TIM2_CH1 的输出才允许送到 PA0。

如果漏掉它，常见现象是 TIM2 在跑、`CCR1` 也在改，但 PA0 没有 PWM。

### 6.15 `EGR.UG` 是什么

`EGR.UG` 是软件产生更新事件。

它属于定时器事件层。本课代码：

```c
TIM2->EGR |= TIM_EGR_UG;
```

这一步让 `PSC`、`ARR`、预装载相关配置进入一致状态。尤其在启动前触发一次更新，可以避免刚开始输出时第一周期使用旧装载状态。

如果省略它，后续通常仍可能跑起来，但启动瞬间的第一个周期可能不是你计算的那个样子。

### 6.16 `HAL_TIM_PWM_Init()` 是什么

`HAL_TIM_PWM_Init()` 是 HAL 的 PWM 定时器初始化接口。

它属于 HAL 外设初始化层。它读取 `htim2.Instance` 和 `htim2.Init`，把 `Prescaler`、`Period`、`CounterMode` 等字段写入 TIM2 基础时基配置。

本课中它对应寄存器版 `TIM2->PSC`、`TIM2->ARR`、计数方向等配置。

如果它失败或没调用，后面的 PWM 通道配置没有可靠的时基基础。

### 6.17 `TIM_OC_InitTypeDef` 是什么

`TIM_OC_InitTypeDef` 是 HAL 描述定时器输出通道参数的结构体。

它属于 HAL 通道配置层。本课字段是：

```c
sConfigOC.OCMode = TIM_OCMODE_PWM1;
sConfigOC.Pulse = 250U;
sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
```

`OCMode` 对应 `OC1M`，`Pulse` 对应 `CCR1`，`OCPolarity` 对应通道输出极性，`OCFastMode` 是快速模式控制，本课不需要。

### 6.18 `HAL_TIM_PWM_ConfigChannel()` 是什么

`HAL_TIM_PWM_ConfigChannel()` 是 HAL 的 PWM 通道配置接口。

它属于 HAL 参数落地层。代码：

```c
HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1)
```

这表示把 `sConfigOC` 的 PWM 参数写到 TIM2_CH1。底层会影响 `CCR1`、`OC1M`、极性等通道寄存器。

如果通道传错，PA0 对应的 CH1 不会得到正确配置。

### 6.19 `HAL_TIM_PWM_Start()` 是什么

`HAL_TIM_PWM_Start()` 是 HAL 的 PWM 启动接口。

它属于 HAL 运行控制层。本课代码：

```c
HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1)
```

它让 TIM2_CH1 PWM 真正开始输出。可以把它对应到寄存器版的 `CC1E` 和 `CEN` 阶段。

只配置 `HAL_TIM_PWM_ConfigChannel()` 而不 Start，PA0 不会输出运行中的 PWM。

### 6.20 `__HAL_TIM_SET_COMPARE()` 是什么

`__HAL_TIM_SET_COMPARE()` 是 HAL 修改定时器通道比较值的宏。

它属于运行期占空比更新层。本课代码：

```c
__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, duty);
```

对 TIM2_CH1 来说，它本质上就是把 `duty` 写入 `CCR1`。这和寄存器版 `TIM2->CCR1 = duty;` 是同一层硬件动作。

如果你传入的通道不是 `TIM_CHANNEL_1`，就不会改 PA0 对应的比较值。

## 7. 寄存器版代码逐步讲解

### 7.1 `delay()` 只控制亮度变化速度

代码：

```c
static void delay(volatile uint32_t count)
{
    while (count--) {
        __NOP();
    }
}
```

这个延时不生成 PWM。PWM 由 TIM2 硬件以 1kHz 输出。`delay()` 只决定主循环多久改一次 `CCR1`，也就是 LED 亮度阶梯变化的速度。

如果你把 delay 改小，亮度变化更快；PWM 频率仍由 `PSC` 和 `ARR` 决定。

### 7.2 系统时钟决定 TIM2 计算基础

`system_clock_72mhz_init()` 和前面课程一样，把系统配置到 72MHz：

```c
FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;
RCC->CR |= RCC_CR_HSEON;
```

等待 HSE 稳定后，代码选择 HSE 进 PLL，9 倍频，最后切换 SYSCLK 到 PLL。

本课后面写 `PSC = 72 - 1`，默认前提就是 TIM2 输入按 72MHz 计算。如果系统时钟没有到 72MHz，PWM 频率就不是 1kHz。

### 7.3 打开 GPIOA 和 AFIO 时钟

代码：

```c
RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN;
```

`IOPAEN` 让 GPIOA 配置寄存器可用。PA0 的模式在 GPIOA 里，所以必须开。

`AFIOEN` 对 F1 复用功能体系有意义。本课没有做重映射，但 PA0 作为 TIM2_CH1 复用输出，保留 AFIO 时钟能让复用相关路线更清楚。

### 7.4 PA0 配置为复用推挽输出

代码：

```c
GPIOA->CRL &= ~(GPIO_CRL_MODE0 | GPIO_CRL_CNF0);
GPIOA->CRL |= GPIO_CRL_MODE0_1 | GPIO_CRL_CNF0_1;
```

PA0 属于 0 到 7 号引脚，所以在 `CRL`。第一句清掉旧模式，第二句设置 `MODE0 = 10`、`CNF0 = 10`。

硬件后果是 PA0 的输出由复用外设 TIM2_CH1 驱动。PWM 不是写 `GPIOA->ODR` 写出来的。

### 7.5 打开 TIM2 时钟

代码：

```c
RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
```

TIM2 挂在 APB1 总线上。没有 `TIM2EN`，TIM2 的寄存器配置和计数功能不会正常工作。

如果 PA0 模式正确但 TIM2 时钟没开，外部仍然看不到 PWM。

### 7.6 设置 `PSC = 72 - 1`

代码：

```c
TIM2->PSC = 72U - 1U;
```

实际分频是 72。按 72MHz 输入计算：

```text
72MHz / 72 = 1MHz
```

这表示 `CNT` 每 1 微秒加 1。后面的 `ARR = 999` 就对应 1000 微秒，也就是 1ms。

### 7.7 设置 `ARR = 1000 - 1`

代码：

```c
TIM2->ARR = 1000U - 1U;
```

`ARR = 999`，`CNT` 一轮包含 1000 个计数。1MHz 计数下，1000 个计数是 1ms。

PWM 周期 = 1ms，PWM 频率 = 1kHz。

### 7.8 设置初始 `CCR1 = 250`

代码：

```c
TIM2->CCR1 = 250U;
```

在 PWM mode 1、有效高的理解下，`CCR1 = 250` 表示每 1000 个计数中前 250 个计数为有效电平，初始占空比约 25%。

主循环之后会覆盖这个值，所以它只是启动初始值，不是最终固定占空比。

### 7.9 清 `CC1S` 和 `OC1M`

代码：

```c
TIM2->CCMR1 &= ~(TIM_CCMR1_CC1S | TIM_CCMR1_OC1M);
```

`CC1S` 决定通道 1 是输入捕获还是输出比较。PWM 需要输出模式，所以 `CC1S = 00`。

`OC1M` 是多 bit 模式字段，先清零再设置能避免旧模式残留。

### 7.10 设置 PWM mode 1

代码：

```c
TIM2->CCMR1 |= TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2;
```

这让 `OC1M = 110`，对应 PWM mode 1。

硬件后果是 TIM2_CH1 不再像上一课那样匹配翻转，而是在每个周期中按 `CNT` 和 `CCR1` 的比较关系形成高低电平。

### 7.11 打开 `OC1PE`

代码：

```c
TIM2->CCMR1 |= TIM_CCMR1_OC1PE;
```

因为主循环会不断写 `CCR1`，打开预装载可以让新比较值在更新事件后生效，波形更稳定。

没有预装载时，周期中间修改比较值可能让当前周期出现不规则变化。

### 7.12 打开通道 1 输出

代码：

```c
TIM2->CCER |= TIM_CCER_CC1E;
```

`CC1E` 打开 TIM2_CH1 输出。它和 PA0 的复用推挽一起，把 PWM 从定时器内部送到外部引脚。

漏掉这句，`CNT` 可以跑，`CCR1` 可以变，但 PA0 没有 PWM。

### 7.13 产生更新事件并启动计数器

代码：

```c
TIM2->EGR |= TIM_EGR_UG;
TIM2->CR1 |= TIM_CR1_CEN;
```

`UG` 让预分频、自动重装载、预装载等配置先同步一次。`CEN` 启动 `CNT`。

这两句执行后，TIM2_CH1 开始按 PWM mode 1 输出。

### 7.14 主循环动态修改占空比

代码：

```c
TIM2->CCR1 = duty;
```

这句是本课现象变化的核心。它不断改变通道 1 比较值，也就是改变 PWM 高电平宽度。

`duty` 从 0 到 1000，步进 50。`ARR + 1 = 1000`，所以每一步约改变 5% 占空比。

### 7.15 `step` 控制亮度变化方向

代码逻辑是：

```c
if ((int32_t)duty + step >= 1000) {
    duty = 1000U;
    step = -50;
} else if ((int32_t)duty + step <= 0) {
    duty = 0U;
    step = 50;
} else {
    duty = (uint16_t)((int32_t)duty + step);
}
```

`step` 为正时亮度往上走，到 1000 后改成负；`step` 为负时亮度往下走，到 0 后改成正。

这只是软件改变 `CCR1` 的策略，不改变 PWM 频率。

## 8. HAL 版代码逐步讲解

### 8.1 `HAL_Init()` 和 SysTick

HAL 版主函数先调用：

```c
HAL_Init();
```

它初始化 HAL 基础状态，并通常配置 SysTick。后面的 `HAL_Delay(40)` 依赖 HAL tick。

这不是 PWM 硬件输出本身，但如果 HAL tick 没正常工作，主循环的占空比更新节奏会出问题。

### 8.2 `hal_msp_init_minimal()` 补齐最小 HAL 工程基础

代码：

```c
__HAL_RCC_AFIO_CLK_ENABLE();
__HAL_RCC_PWR_CLK_ENABLE();
```

在 CubeMX 工程里，这类 MSP 初始化常由生成文件处理。当前 PlatformIO 最小工程只有 `main.c`，所以手动补上。

本课 TIM2_CH1 默认映射到 PA0，不需要重映射；但开启 AFIO 是 F1 HAL 工程常见基础动作，也方便后续课程加入复用重映射。

### 8.3 HAL 时钟配置和寄存器版目标一致

HAL 版用 `RCC_OscInitTypeDef` 和 `RCC_ClkInitTypeDef` 配置 HSE、PLL、SYSCLK、AHB、APB1、APB2。

这些结构体最终对应 RCC 和 FLASH 寄存器。目标仍然是 72MHz 系统时钟，以及后续按 72MHz 计算 TIM2 的 PWM 参数。

### 8.4 `GPIO_InitTypeDef` 配置 PA0

代码：

```c
gpio.Pin = GPIO_PIN_0;
gpio.Mode = GPIO_MODE_AF_PP;
gpio.Pull = GPIO_NOPULL;
gpio.Speed = GPIO_SPEED_FREQ_LOW;
HAL_GPIO_Init(GPIOA, &gpio);
```

`GPIO_PIN_0` 选择 PA0。`GPIO_MODE_AF_PP` 对应复用推挽输出。`GPIO_NOPULL` 表示不启用内部上下拉。`GPIO_SPEED_FREQ_LOW` 对应输出速度选择。

`HAL_GPIO_Init()` 最终会写 `GPIOA->CRL` 里 PA0 的配置位。

### 8.5 `htim2.Instance = TIM2`

代码：

```c
htim2.Instance = TIM2;
```

这告诉 HAL：后续所有 `htim2` 配置都落到 TIM2 外设。它对应寄存器版里的 `TIM2->...`。

如果实例选错，可能配置到另一个定时器，PA0 上当然看不到 TIM2_CH1 PWM。

### 8.6 `Prescaler` 对应 `PSC`

代码：

```c
htim2.Init.Prescaler = 72U - 1U;
```

对应寄存器版：

```c
TIM2->PSC = 72U - 1U;
```

硬件意义是把 72MHz 分成 1MHz 计数频率。

### 8.7 `Period` 对应 `ARR`

代码：

```c
htim2.Init.Period = 1000U - 1U;
```

对应寄存器版：

```c
TIM2->ARR = 1000U - 1U;
```

硬件意义是每 1000 个计数形成一个 PWM 周期，所以频率是 1kHz。

### 8.8 `HAL_TIM_PWM_Init()` 初始化 TIM2 PWM 时基

代码：

```c
if (HAL_TIM_PWM_Init(&htim2) != HAL_OK) {
    error_handler();
}
```

它把 `htim2.Init` 中的时基字段写到 TIM2。`error_handler()` 是错误兜底，如果 HAL 初始化失败，程序停住，避免继续运行一个未知状态的外设。

### 8.9 `OCMode = TIM_OCMODE_PWM1`

代码：

```c
sConfigOC.OCMode = TIM_OCMODE_PWM1;
```

对应寄存器版 `OC1M = 110`。

它决定 CH1 用 PWM mode 1，而不是 toggle、强制高、强制低或其他模式。

### 8.10 `Pulse = 250`

代码：

```c
sConfigOC.Pulse = 250U;
```

对应寄存器版初始：

```c
TIM2->CCR1 = 250U;
```

`Pulse` 在 PWM 语境里就是初始脉宽，也就是初始比较值。`ARR + 1 = 1000` 时，250 大约是 25%。

### 8.11 `OCPolarity` 和 `OCFastMode`

代码：

```c
sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
```

`OCPolarity = HIGH` 表示有效电平按高电平输出。它影响占空比和实际引脚高低电平之间的关系。

`OCFastMode` 是输出比较快速模式，本课不需要快速响应特殊触发，所以关闭。

### 8.12 `HAL_TIM_PWM_ConfigChannel()` 配置 CH1

代码：

```c
HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1)
```

这一步把 `sConfigOC` 写到 TIM2_CH1。它对应寄存器版设置 `CCR1`、`OC1M`、通道极性等。

`TIM_CHANNEL_1` 不能随便换，因为 PA0 默认对应 TIM2_CH1。

### 8.13 `HAL_TIM_PWM_Start()` 启动 PWM 输出

代码：

```c
HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1)
```

它打开 TIM2_CH1 的输出并启动计数。对应寄存器版 `CC1E` 和 `CEN` 所在阶段。

如果忘记调用这句，PA0 不会有运行中的 PWM。

### 8.14 `__HAL_TIM_SET_COMPARE()` 动态改占空比

代码：

```c
__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, duty);
```

这句对应寄存器版：

```c
TIM2->CCR1 = duty;
```

它每 40ms 左右更新一次比较值，让 LED 亮度按台阶变化。

### 8.15 `SysTick_Handler()` 为什么出现在本课

代码：

```c
void SysTick_Handler(void)
{
    HAL_IncTick();
}
```

这是 HAL 延时链路的一部分。`HAL_Delay()` 依赖 tick 递增。如果最小工程里缺少这个中断处理函数，程序可能进入默认中断处理，表现为卡住。

它不生成 PWM，但会影响主循环能否按节奏更新 `duty`。

## 9. 两个版本真正应该怎么学

寄存器版先抓公式：

```text
TIM2 计数频率 = TIM2 输入时钟 / (PSC + 1)
PWM 频率 = TIM2 计数频率 / (ARR + 1)
占空比约等于 CCR1 / (ARR + 1)
```

再抓通道输出链路：

```text
PA0 复用推挽 -> TIM2_CH1 -> PWM mode 1 -> CC1E -> CEN
```

HAL 版要把每个字段翻译回寄存器：

```text
Prescaler -> PSC
Period -> ARR
Pulse -> CCR1
TIM_OCMODE_PWM1 -> OC1M = 110
TIM_CHANNEL_1 -> TIM2_CH1
HAL_TIM_PWM_Start -> 打开通道并启动计数
__HAL_TIM_SET_COMPARE -> 运行中写 CCR1
```

这样看 HAL，才不会觉得它只是几个神秘 API。

## 10. 检验问题清单

### 10.1 `ARR = 999` 时，为什么周期是 1000 个计数？

因为向上计数从 0 开始，包含 0 到 999，一共 1000 个值。所以周期计算用 `ARR + 1`。

### 10.2 本课 PWM 频率怎么算？

`PSC = 72 - 1`，所以 72MHz / 72 = 1MHz。`ARR = 1000 - 1`，所以 1MHz / 1000 = 1kHz。

### 10.3 `CCR1 = 500` 时占空比约是多少？

`ARR + 1 = 1000`，所以 `500 / 1000 = 50%`。在 PA0 高电平点亮 LED 的接法下，亮度大约处于中间水平。

### 10.4 主循环改 `CCR1` 为什么能改 LED 亮度？

因为 `CCR1` 决定每个 PWM 周期内有效电平持续多久。改 `CCR1` 就是在改占空比，LED 的平均电流随占空比变化。

### 10.5 改 `delay()` 会改变 PWM 频率吗？

不会。`delay()` 只改变主循环更新占空比的速度。PWM 频率由 TIM2 的 `PSC` 和 `ARR` 决定。

### 10.6 PA0 配成普通推挽输出会怎样？

PA0 不会正确接收 TIM2_CH1 的 PWM 输出。你可能能用 GPIO 手动控制它，但定时器通道波形无法按本课链路输出到引脚。

### 10.7 HAL 里的 `Pulse` 对应什么？

`Pulse` 对应通道比较寄存器。本课是 `TIM_CHANNEL_1`，所以对应 `CCR1`。

### 10.8 `__HAL_TIM_SET_COMPARE()` 对应寄存器版哪句？

对应 `TIM2->CCR1 = duty;`。它根据定时器句柄和通道选择正确的 CCR 寄存器。

## 11. 工程实现步骤

### 11.1 需求分析

本课需求是输出 1kHz PWM，并让占空比在运行中变化。

要实现这个需求，必须同时满足：

- TIM2 有稳定计数时钟。
- PA0 能输出 TIM2_CH1。
- TIM2 周期是 1ms。
- CH1 使用 PWM mode 1。
- `CCR1` 可在主循环中更新。
- 通道输出和计数器已经启动。

### 11.2 硬件核查

检查 PA0、限流电阻、LED、GND 是否接对。

如果按推荐接法 PA0 -> 电阻 -> LED -> GND，那么 PA0 高电平时 LED 亮。占空比越大越亮。

如果你用示波器，探头接 PA0，地夹接开发板 GND。不要用 PC13 判断 PWM 波形，PC13 本课没有参与 PWM。

### 11.3 寄存器路线

寄存器版按这个顺序实现：

1. 配置系统时钟到 72MHz。
2. 打开 GPIOA、AFIO 时钟。
3. 配置 PA0 为复用推挽输出。
4. 打开 TIM2 时钟。
5. 写 `PSC = 72 - 1`。
6. 写 `ARR = 1000 - 1`。
7. 写初始 `CCR1 = 250`。
8. 设置 CH1 为输出模式。
9. 设置 `OC1M = 110` PWM mode 1。
10. 设置 `OC1PE`。
11. 设置 `CC1E`。
12. 触发 `EGR.UG`。
13. 设置 `CR1.CEN`。
14. 主循环写 `TIM2->CCR1 = duty`。

### 11.4 HAL 路线

HAL 版按这个顺序实现：

1. `HAL_Init()`。
2. 补齐最小 MSP 初始化。
3. 配置系统时钟。
4. 打开 GPIOA 时钟并配置 PA0 为 `GPIO_MODE_AF_PP`。
5. 打开 TIM2 时钟。
6. `htim2.Instance = TIM2`。
7. 设置 `Prescaler = 72 - 1`。
8. 设置 `Period = 1000 - 1`。
9. 调用 `HAL_TIM_PWM_Init()`。
10. 设置 `TIM_OC_InitTypeDef` 的 `OCMode`、`Pulse`、`OCPolarity`。
11. 调用 `HAL_TIM_PWM_ConfigChannel()` 配置 `TIM_CHANNEL_1`。
12. 调用 `HAL_TIM_PWM_Start()`。
13. 主循环调用 `__HAL_TIM_SET_COMPARE()`。

### 11.5 工程思维

PWM 的工程价值在于把高速、周期性的电平控制交给硬件。CPU 不需要每 1ms 进入一次中断，更不需要在主循环里用延时精确制造高低电平。

CPU 在本课只做两件事：初始化 PWM 规则，以及低速更新占空比。真正的 1kHz 波形由 TIM2 自动输出。

这也是嵌入式里常见的分工：硬件负责严格时序，软件负责改变参数。

### 11.6 常见工程陷阱

第一个陷阱是把亮度变化速度误认为 PWM 频率。主循环延时影响的是占空比更新速度，不是 PWM 周期。

第二个陷阱是忘记 PA0 复用推挽。TIM2_CH1 内部有 PWM，不代表引脚一定输出。

第三个陷阱是 `CCR1` 超过 `ARR + 1` 太多。占空比计算会失去直观意义，波形可能长期保持某个状态。

第四个陷阱是 HAL 只配置通道，不调用 `HAL_TIM_PWM_Start()`。

第五个陷阱是 LED 接法反相，导致你以为占空比越大越暗。先看 LED 到底接 GND 还是 VCC。

## 12. 运行现象

寄存器版和 HAL 版下载后，PA0 都会输出约 1kHz PWM。

如果按推荐接法连接 LED，LED 亮度会按台阶逐渐变亮，到最亮后再逐渐变暗，循环往复。

如果用示波器观察，周期约为 1ms。占空比会随时间变化：从接近 0%，逐步到接近 100%，再回到接近 0%。

寄存器版每次延时由空循环决定，HAL 版每次更新间隔约 40ms，所以两者亮度变化速度可能不完全一样，但 PWM 频率和占空比原理相同。

## 13. 常见问题排查

### 13.1 LED 完全不亮

先查硬件接线：PA0、限流电阻、LED 极性、GND 是否正确。

再查软件链路：GPIOA 时钟、PA0 复用推挽、TIM2 时钟、`CC1E`、`CEN` 是否配置。

如果示波器上 PA0 有 PWM，但 LED 不亮，问题多半在 LED 接法或电阻连接。

### 13.2 LED 亮但亮度不变化

检查主循环是否持续更新比较值。

寄存器版看：

```c
TIM2->CCR1 = duty;
```

HAL 版看：

```c
__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, duty);
```

如果 PWM 固定在某个占空比，说明初始化可能成功，但运行期更新没有生效。

### 13.3 PWM 频率不对

重新计算：

```text
TIM2 计数频率 = 72MHz / (PSC + 1)
PWM 频率 = TIM2 计数频率 / (ARR + 1)
```

确认系统时钟确实是 72MHz，确认 `PSC` 和 `ARR` 没写错。APB1 定时器时钟也要和课程计算保持一致。

### 13.4 占空比和亮度关系反了

先看 LED 接法。如果 LED 接到 VCC，再由 PA0 拉低点亮，那么 PA0 低电平时间越长越亮；这会让 PWM mode 1 的亮度直觉反过来。

再看 `OCPolarity` 是否为 `TIM_OCPOLARITY_HIGH`，寄存器版是否没有改通道极性。

### 13.5 HAL 版程序卡住

如果 HAL 初始化失败，代码会进入 `error_handler()`。检查系统时钟配置、HSE 是否存在、PlatformIO 板卡配置是否和硬件一致。

如果 `HAL_Delay()` 卡住，检查 `SysTick_Handler()` 是否存在并调用 `HAL_IncTick()`。

### 13.6 示波器有 PWM，但肉眼看不出变化

可能是占空比更新太快或太慢，也可能 LED 接法、亮度范围不明显。

寄存器版可以调 `delay(180000U)`，HAL 版可以调 `HAL_Delay(40)`。注意这只改变亮度变化速度，不改变 PWM 频率。

## 14. 本课最核心的结论

PWM 的本质是：定时器在固定周期内比较 `CNT` 和 `CCR1`，用比较结果决定输出电平。

本课中：

- `PSC` 把 TIM2 计数变成 1MHz。
- `ARR` 把 PWM 周期变成 1ms，也就是 1kHz。
- `CCR1` 决定每个周期内有效电平持续多久。
- `OC1M = 110` 选择 PWM mode 1。
- `OC1PE` 让运行中更新比较值更规整。
- `CC1E` 和 PA0 复用推挽让 PWM 真正出现在引脚。

## 15. 建议你现在怎么读这节课

先把三个数字算清楚：

```text
72MHz / 72 = 1MHz
1MHz / 1000 = 1kHz
CCR1 / 1000 = 占空比
```

再回到代码找三条线：

- PA0 怎么变成 TIM2_CH1 输出脚。
- TIM2 怎么生成 1kHz 周期。
- 主循环怎么通过改 `CCR1` 改亮度。

最后看 HAL 版，把 `Prescaler`、`Period`、`Pulse`、`__HAL_TIM_SET_COMPARE()` 翻译回寄存器。

## 16. 扩展练习

1. 把 `duty` 固定为 100、500、900，观察 LED 亮度差异。
2. 把步进 `50` 改成 `10`，观察亮度变化是否更细。
3. 把步进 `50` 改成 `100`，观察亮度台阶是否更明显。
4. 把 PWM 改成 500Hz，重新计算 `PSC` 和 `ARR`。
5. 把 PWM 改成 2kHz，观察 LED 肉眼效果是否明显变化。
6. 用示波器验证周期是否约 1ms。
7. 尝试把 PWM mode 1 改成 PWM mode 2，观察亮度变化方向。

## 17. 下一课预告

上一课：[07_timer_output_compare](../07_timer_output_compare/README.md)

下一课：[09_pwm_advanced](../09_pwm_advanced/README.md)

下一课会在 PWM 基础上做更像“呼吸灯”的占空比变化。硬件仍然是 TIM2_CH1 和 PA0，但软件更新 `CCR1` 的策略会更有节奏感。

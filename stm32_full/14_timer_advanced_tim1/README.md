# 14_timer_advanced_tim1 - TIM1 高级定时器

## 1. 本课到底在学什么

本课表面现象是：TIM1_CH1 在 PA8 输出约 1kHz、30% 占空比 PWM，同时 PC13 做主循环心跳闪烁。

真正要学的是高级定时器 TIM1 和普通定时器 TIM2/TIM3 的输出差别。普通定时器配置 PWM 模式、打开通道、配置复用引脚后通常就能输出；TIM1 这类高级定时器还多了一道主输出使能 `BDTR.MOE`。如果 `MOE` 没打开，即使 `CC1E` 已经打开，PA8 也不会真正输出 PWM。

这节课接在一系列 TIM 课程之后。前面我们已经学过 PWM、输入捕获、PWM 输入、编码器接口；本课开始接触高级定时器面向电机和功率驱动的安全输出结构。后续如果做互补 PWM、死区、刹车保护，TIM1/TIM8 的这些特性都会非常关键。

## 2. 本课学习目标

学完本课，你应该能做到：

- 解释 TIM1 为什么叫高级定时器。
- 说明 PA8 为什么是 TIM1_CH1 的默认复用输出引脚。
- 根据 `PSC = 72 - 1`、`ARR = 1000 - 1` 算出 PWM 频率。
- 根据 `CCR1 = 300` 算出约 30% 占空比。
- 看懂 `OC1M = PWM1`、`OC1PE`、`CC1E` 在 TIM1 PWM 输出里的作用。
- 解释 `BDTR.MOE` 为什么是高级定时器输出到引脚前的总闸。
- 区分 `CC1E` 通道输出使能和 `MOE` 主输出使能。
- 看懂 HAL 版 `TIM_BreakDeadTimeConfigTypeDef` 对应 BDTR。
- 说明 `Break`、`DeadTime`、`AutomaticOutput` 是为哪类工程场景准备的，并区分 `AutomaticOutput` 和 `MOE`。

## 3. 本课目录结构

```text
14_timer_advanced_tim1/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

`reg/` 直接配置 PA8、TIM1_CH1 PWM 和 `TIM1->BDTR = TIM_BDTR_MOE`。

`hal/` 用 `HAL_TIM_PWM_Init()`、`HAL_TIM_PWM_ConfigChannel()`、`HAL_TIMEx_ConfigBreakDeadTime()` 和 `HAL_TIM_PWM_Start()` 完成同样输出。

两份工程都使用 `genericSTM32F103C8`、`stm32cube`、`stlink`，并定义 `HSE_VALUE=8000000U`。

## 4. 实验硬件

本课使用：

- STM32F103C8T6 BluePill
- ST-Link 下载器
- PA8 引脚
- PC13 板载 LED
- 示波器或逻辑分析仪
- 可选：外接 LED 和限流电阻

PA8 是 TIM1_CH1 的默认输出脚。推荐用示波器观察 PA8，因为 PWM 频率是 1kHz，仪器能直接看到周期和占空比。

如果外接 LED，必须串联限流电阻。按 PA8 -> 电阻 -> LED -> GND 接法时，占空比越大平均亮度越高。

## 5. 先建立一个最基本的脑图

本课按六层拆开看。

现象层：PA8 输出约 1kHz PWM，占空比约 30%；PC13 周期性翻转，表示主循环仍在运行。

物理/硬件层：PA8 是芯片引脚，也是 TIM1_CH1 默认复用输出脚。要让 TIM1_CH1 控制 PA8，PA8 必须配置为复用推挽输出。

芯片模块层：RCC 给 GPIOA、TIM1、GPIOC 送时钟；寄存器版还显式打开 AFIO，让 F1 复用功能链路更完整。GPIOA 配 PA8；TIM1 生成 PWM；BDTR 控制高级定时器输出安全门；GPIOC 控制 PC13。

寄存器/bit 层：`PSC` 决定计数频率，`ARR` 决定周期，`CCR1` 决定占空比，`CCMR1.OC1M` 选择 PWM mode 1，`CCER.CC1E` 打开通道，`BDTR.MOE` 打开主输出，`CR1.CEN` 启动计数器。

C/CMSIS 层：寄存器版通过 `TIM1->PSC`、`TIM1->ARR`、`TIM1->CCR1`、`TIM1->BDTR` 直接写 TIM1 寄存器，通过 `TIM_BDTR_MOE` 这种宏定位关键 bit。

HAL/工程层：HAL 版用 `TIM_HandleTypeDef` 描述 TIM1，用 `TIM_OC_InitTypeDef` 描述 PWM 通道，用 `TIM_BreakDeadTimeConfigTypeDef` 描述 BDTR 安全输出配置。

完整链路是：

1. 系统时钟配置到 72MHz。
2. PC13 配成普通输出，作为心跳。
3. 打开 GPIOA、TIM1、AFIO 时钟。
4. PA8 配成复用推挽输出。
5. TIM1 设置 `PSC = 72 - 1`，计数频率 1MHz。
6. TIM1 设置 `ARR = 1000 - 1`，PWM 周期 1ms，也就是 1kHz。
7. TIM1 设置 `CCR1 = 300`，占空比约 30%。
8. `OC1M = 110`，选择 PWM mode 1。
9. `OC1PE = 1`，打开比较值预装载。
10. `CC1E = 1`，打开通道 1 输出。
11. `BDTR.MOE = 1`，打开高级定时器主输出。
12. `ARPE = 1`，打开 ARR 预装载。
13. `EGR.UG` 触发更新，让配置装载。
14. `CEN = 1`，启动计数器。
15. PA8 才真正输出 TIM1_CH1 PWM。

## 6. 核心名词解释

### 6.1 `TIM1` 是什么

`TIM1` 是 STM32F103 的高级控制定时器。

它属于 STM32 片上高级 TIM 外设层。它能做普通 PWM，也支持互补输出、死区时间、刹车输入、重复计数器和主输出使能。

本课用 TIM1_CH1 输出 PWM，重点观察它比 TIM2/TIM3 多出的 `BDTR.MOE`。

如果把 TIM1 当普通定时器，只配到 `CC1E` 就停手，PA8 可能没有波形。

### 6.2 高级定时器是什么

高级定时器是面向电机控制、功率驱动等场景的定时器。

它属于定时器功能分级层。相较普通定时器，高级定时器多了主输出控制、互补输出、死区、刹车、重复计数器等功能。

这些功能的目的不是让点灯更复杂，而是让半桥、全桥、电机驱动更安全。

本课只引入最关键的主输出使能，不展开互补输出和刹车应用。

### 6.3 `PA8` 是什么

PA8 是 GPIOA 的 8 号引脚。

它属于物理引脚层和复用输出层。本课中 PA8 是 TIM1_CH1 的默认复用输出脚。

寄存器版在 `GPIOA->CRH` 中配置 PA8，因为 8 到 15 号引脚属于 CRH。HAL 版使用 `GPIO_PIN_8` 和 `GPIO_MODE_AF_PP`。

如果测错引脚，比如去看 PA0，就不会看到 TIM1_CH1 的 PWM。

### 6.4 复用推挽输出是什么

复用推挽输出表示引脚输出由片上外设驱动，驱动方式是推挽。

它属于 GPIO 模式层。本课 PA8 要输出 TIM1_CH1，所以不能配置成普通 GPIO 输出。

寄存器版：

```c
GPIOA->CRH |= GPIO_CRH_MODE8_1 | GPIO_CRH_CNF8_1;
```

`MODE8=10` 表示输出速度配置，`CNF8=10` 表示复用推挽。

### 6.5 `PSC` 是什么

`PSC` 是 Prescaler，中文叫预分频器。

它属于 TIM1 时基层。本课：

```c
TIM1->PSC = 72U - 1U;
```

TIM1 挂在 APB2，APB2 不分频，TIM1 输入按 72MHz 计算。除以 72 后，计数频率是 1MHz。

如果 `PSC` 写错，PWM 频率会整体改变。

### 6.6 `ARR` 是什么

`ARR` 是 Auto-Reload Register，中文叫自动重装载寄存器。

它属于 PWM 周期层。本课：

```c
TIM1->ARR = 1000U - 1U;
```

`ARR=999` 表示 1000 个 1MHz tick 形成一个周期，PWM 频率是 1kHz。

### 6.7 `CCR1` 是什么

`CCR1` 是 Capture/Compare Register 1。

它属于 TIM1_CH1 比较值层。在 PWM 输出中，`CCR1` 决定有效电平持续时间。

本课：

```c
TIM1->CCR1 = 300U;
```

`ARR+1=1000`，所以占空比约 `300/1000 = 30%`。

### 6.8 `OC1M` 是什么

`OC1M` 是 Output Compare 1 Mode，中文叫输出比较 1 模式。

它属于 `CCMR1` 中的通道输出模式字段。本课设置 `OC1M=110`，也就是 PWM mode 1。

寄存器版：

```c
TIM1->CCMR1 = TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1PE;
```

如果不是 PWM mode 1，PA8 的输出不会按 `CCR1` 形成预期占空比。

### 6.9 `OC1PE` 是什么

`OC1PE` 是 Output Compare 1 Preload Enable。

它属于比较值预装载层。打开后，`CCR1` 新值可以在更新事件同步生效，避免周期中间突然改变输出规则。

本课虽然没有动态改占空比，但保留 `OC1PE` 是 PWM 配置的常见习惯。

### 6.10 `CCER.CC1E` 是什么

`CC1E` 是 Capture/Compare 1 Output Enable，中文叫通道 1 输出使能。

它属于通道输出开关层。本课：

```c
TIM1->CCER = TIM_CCER_CC1E;
```

它打开 TIM1_CH1 通道输出。对普通定时器，这一步通常已经足以把通道输出送到复用脚；但 TIM1 还需要 `MOE`。

### 6.11 `BDTR` 是什么

`BDTR` 是 Break and Dead-Time Register，中文叫刹车与死区时间寄存器。

它属于高级定时器安全输出层。TIM1/TIM8 的刹车、死区、主输出使能等都在这里。

本课最关键的是 `BDTR.MOE`。HAL 版用 `TIM_BreakDeadTimeConfigTypeDef` 和 `HAL_TIMEx_ConfigBreakDeadTime()` 配置这个寄存器相关功能。

### 6.12 `MOE` 是什么

`MOE` 是 Main Output Enable，中文叫主输出使能。

它属于高级定时器总输出门控层。TIM1 的 PWM 内部生成后，还必须经过 `MOE` 这道总闸，才能真正输出到 PA8。

寄存器版：

```c
TIM1->BDTR = TIM_BDTR_MOE;
```

如果 `MOE=0`，TIM1 内部计数和比较可能都正常，但 PA8 没有 PWM。

### 6.13 `DeadTime` 是什么

`DeadTime` 是死区时间。

它属于功率驱动保护层。半桥驱动中，上管和下管不能同时导通，否则会短路。死区时间用于在互补输出切换时插入一小段双方都关断的时间。

本课只输出 TIM1_CH1，不做互补输出，所以 HAL 版 `DeadTime = 0`。

### 6.14 `Break` 是什么

`Break` 是刹车输入。

它属于高级定时器故障保护层。当外部故障信号触发时，高级定时器可以快速关闭输出，保护电机或功率器件。

本课 HAL 版 `BreakState = TIM_BREAK_DISABLE`，表示不使用刹车输入。

### 6.15 `RepetitionCounter` 是什么

`RepetitionCounter` 是重复计数器。

它属于高级定时器更新控制层。它可以让更新事件不是每个周期都发生，而是隔若干周期才发生一次。

本课 HAL 版：

```c
htim1.Init.RepetitionCounter = 0;
```

表示每个 PWM 周期都按普通方式更新。

### 6.16 `TIM_BreakDeadTimeConfigTypeDef` 是什么

这是 HAL 的 Break/Dead-Time 配置结构体。

它属于 HAL 高级定时器安全配置层。字段包括 `DeadTime`、`BreakState`、`BreakPolarity`、`AutomaticOutput` 等。

它对应底层 BDTR。普通 TIM2/TIM3 PWM 课里没有这个结构体，因为普通定时器没有这套高级输出安全门。

### 6.17 `HAL_TIMEx_ConfigBreakDeadTime()` 是什么

这是 HAL 配置高级定时器 BDTR 的扩展接口。

它属于 HAL TIM 扩展层。本课：

```c
HAL_TIMEx_ConfigBreakDeadTime(&htim1, &bd);
```

它把 `TIM_BreakDeadTimeConfigTypeDef` 中的死区、刹车、自动输出等参数写入 TIM1 的 BDTR。

如果 TIM1 PWM 没有正确处理 BDTR/MOE，PA8 可能无输出。

## 7. 寄存器版代码逐步讲解

### 7.1 系统时钟初始化

`system_clock_72mhz_init()` 打开 HSE，等待稳定，配置 PLL x9，把系统时钟切到 72MHz。

TIM1 挂在 APB2，APB2 本课不分频，所以 TIM1 时钟按 72MHz 计算。

### 7.2 PC13 心跳初始化

代码打开 GPIOC 时钟，把 PC13 配成推挽输出，并初始写高电平熄灭 LED。

PC13 只说明主循环还活着。PA8 有没有 PWM，要看 TIM1 输出链路。

### 7.3 打开 GPIOA、TIM1、AFIO 时钟

代码分两处打开：

```c
RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN;
...
RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;
```

GPIOA 管 PA8，TIM1 生成 PWM，AFIO 参与 F1 的复用功能体系。本课不做重映射，HAL 版不需要单独操作 AFIO；寄存器版显式打开它，是为了让“GPIO 复用输出”这条链路更完整。

### 7.4 PA8 配成复用推挽

代码：

```c
GPIOA->CRH &= ~(GPIO_CRH_MODE8 | GPIO_CRH_CNF8);
GPIOA->CRH |= GPIO_CRH_MODE8_1 | GPIO_CRH_CNF8_1;
```

PA8 属于 CRH。`MODE8=10`，`CNF8=10`，表示复用推挽输出。TIM1_CH1 才能驱动 PA8。

### 7.5 设置 PWM 频率和占空比

代码：

```c
TIM1->PSC = 72U - 1U;
TIM1->ARR = 1000U - 1U;
TIM1->CCR1 = 300U;
```

72MHz / 72 = 1MHz。1MHz / 1000 = 1kHz。`CCR1=300` 时，占空比约 30%。

### 7.6 设置 PWM mode 1 和预装载

代码：

```c
TIM1->CCMR1 = TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1PE;
```

`OC1M=110`，选择 PWM mode 1。`OC1PE=1`，打开比较值预装载。

### 7.7 打开通道 1 输出

代码：

```c
TIM1->CCER = TIM_CCER_CC1E;
```

这打开 TIM1_CH1 通道输出。但对 TIM1 来说，这还不是最后一步。

### 7.8 打开主输出使能 `MOE`

代码：

```c
TIM1->BDTR = TIM_BDTR_MOE;
```

这是高级定时器最容易漏的一句。`CC1E` 打开的是通道门，`MOE` 打开的是高级定时器总输出门。

少了它，PA8 没有 PWM。

### 7.9 启动计数并产生更新事件

代码：

```c
TIM1->CR1 |= TIM_CR1_ARPE;
TIM1->EGR = TIM_EGR_UG;
TIM1->CR1 |= TIM_CR1_CEN;
```

`ARPE` 打开 ARR 预装载，`UG` 让配置装载，`CEN` 启动计数器。

至此 PA8 输出链路完整。

### 7.10 主循环心跳

代码：

```c
while (1) {
    pc13_toggle();
    delay_cycles(3600000U);
}
```

主循环不参与 PA8 PWM。TIM1 硬件在后台持续输出。

## 8. HAL 版代码逐步讲解

### 8.1 `HAL_Init()` 和时钟配置

HAL 版先 `HAL_Init()`，再用 RCC 结构体配置 HSE、PLL、SYSCLK、AHB、APB1、APB2。

目标和寄存器版一样：系统 72MHz，TIM1 按 72MHz 配 PWM 参数。

### 8.2 HAL 配置 PC13

`GPIO_InitTypeDef` 把 PC13 配成 `GPIO_MODE_OUTPUT_PP`，初始 `GPIO_PIN_SET` 熄灭 LED。

PC13 是心跳，不是 TIM1 输出。

### 8.3 HAL 配置 PA8

代码：

```c
gpio.Pin = GPIO_PIN_8;
gpio.Mode = GPIO_MODE_AF_PP;
gpio.Speed = GPIO_SPEED_FREQ_HIGH;
HAL_GPIO_Init(GPIOA, &gpio);
```

PA8 配成复用推挽输出。`GPIO_SPEED_FREQ_HIGH` 适合定时器输出引脚。

### 8.4 `htim1.Init` 配 TIM1 时基

代码：

```c
htim1.Instance = TIM1;
htim1.Init.Prescaler = 72U - 1U;
htim1.Init.Period = 1000U - 1U;
htim1.Init.RepetitionCounter = 0;
HAL_TIM_PWM_Init(&htim1);
```

`Prescaler` 对应 `PSC`，`Period` 对应 `ARR`，`RepetitionCounter` 是 TIM1 高级定时器特有字段。

### 8.5 `TIM_OC_InitTypeDef` 配 CH1 PWM

代码：

```c
oc.OCMode = TIM_OCMODE_PWM1;
oc.Pulse = 300U;
oc.OCPolarity = TIM_OCPOLARITY_HIGH;
oc.OCFastMode = TIM_OCFAST_DISABLE;
HAL_TIM_PWM_ConfigChannel(&htim1, &oc, TIM_CHANNEL_1);
```

`OCMode` 对应 `OC1M=110`，`Pulse` 对应 `CCR1=300`，`OCPolarity` 对应输出有效极性。

### 8.6 `TIM_BreakDeadTimeConfigTypeDef` 配 BDTR

代码里设置：

```c
bd.DeadTime = 0;
bd.BreakState = TIM_BREAK_DISABLE;
bd.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
```

这些字段描述 TIM1 的高级输出安全配置。`DeadTime=0` 表示不插死区，`BreakState=DISABLE` 表示不启用刹车输入，`AutomaticOutput=DISABLE` 表示本课不使用刹车释放后的自动输出恢复。它对应的是 BDTR 的 `AOE`，不要把它和主输出使能 `MOE` 混成同一个开关。

### 8.7 `HAL_TIMEx_ConfigBreakDeadTime()`

代码：

```c
HAL_TIMEx_ConfigBreakDeadTime(&htim1, &bd);
```

这个函数配置 TIM1 的 BDTR。普通 TIM2/TIM3 没有这个步骤。

### 8.8 `HAL_TIM_PWM_Start()`

代码：

```c
HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
```

它启动 TIM1_CH1 PWM 输出。对高级定时器，CubeF1 HAL 的 `HAL_TIM_PWM_Start()` 会打开通道输出，并在 TIM1 这类带 Break/Dead-Time 的定时器上设置 `MOE`。

如果配置了 BDTR 但不 Start，PA8 仍然不会输出。

### 8.9 `SysTick_Handler()` 和主循环

HAL 版 `SysTick_Handler()` 调用 `HAL_IncTick()`，让 `HAL_Delay(500)` 能工作。

主循环每 500ms 翻转 PC13。PA8 PWM 由 TIM1 硬件持续输出。

## 9. 两个版本真正应该怎么学

先按普通 PWM 读：

```text
PA8 复用推挽
PSC/ARR/CCR1
PWM mode 1
CC1E
CEN
```

然后停下来补 TIM1 特有的一步：

```text
BDTR.MOE
```

HAL 版也一样。普通 PWM 初始化之外，多看 `TIM_BreakDeadTimeConfigTypeDef` 和 `HAL_TIMEx_ConfigBreakDeadTime()`。

本课的关键不是 PWM 公式本身，而是高级定时器输出路径多了一道安全门。

## 10. 检验问题清单

### 10.1 TIM1_CH1 为什么输出到 PA8？

答：PA8 是 STM32F103 上 TIM1_CH1 的默认复用输出引脚。本课没有做重映射，所以观察 PA8。

### 10.2 本课 PWM 频率是多少？

答：TIM1 输入按 72MHz，`PSC=72-1` 后是 1MHz，`ARR=1000-1` 后是 1kHz。

### 10.3 `CCR1=300` 时占空比是多少？

答：`ARR+1=1000`，所以占空比约 `300/1000 = 30%`。

### 10.4 只打开 `CC1E` 不打开 `MOE` 会怎样？

答：TIM1 内部可能已经生成 PWM，但高级定时器总输出门没开，PA8 不会真正输出波形。

### 10.5 `BDTR` 是为哪些应用准备的？

答：主要为电机控制、半桥/全桥功率驱动、电源控制等需要安全关断和互补输出的应用准备。

### 10.6 HAL 版哪个结构体对应 BDTR？

答：`TIM_BreakDeadTimeConfigTypeDef` 对应 BDTR 的刹车、死区、锁定、自动输出等配置。

### 10.7 PC13 正常闪能证明 PA8 PWM 正常吗？

答：不能。PC13 只说明主循环运行。PA8 还依赖 GPIOA、TIM1、PA8 复用、`CC1E`、`MOE`。

### 10.8 `DeadTime=0` 表示什么？

答：表示不插入死区时间。本课只做单路 CH1 PWM，不做互补半桥驱动，所以设为 0。

## 11. 工程实现步骤

### 11.1 需求分析

需求是让 TIM1_CH1 通过 PA8 输出 1kHz、30% PWM。

这要求 PA8 是复用输出，TIM1 时基正确，CH1 PWM 模式正确，通道输出打开，TIM1 主输出打开，计数器启动。

### 11.2 硬件核查

确认你测的是 PA8。用示波器时探头接 PA8，地夹接开发板 GND。

如果外接 LED，必须串限流电阻。PC13 闪烁不能替代 PA8 波形测量。

### 11.3 寄存器路线

寄存器版按这个顺序实现：

1. 配置系统时钟。
2. 配置 PC13 输出。
3. 打开 GPIOA、TIM1、AFIO 时钟。
4. PA8 配成复用推挽输出。
5. 写 `PSC=72-1`。
6. 写 `ARR=1000-1`。
7. 写 `CCR1=300`。
8. 设置 `OC1M=PWM1` 和 `OC1PE`。
9. 设置 `CC1E`。
10. 设置 `BDTR.MOE`。
11. 设置 `ARPE` 和 `CEN`。
12. 写 `EGR.UG`。

### 11.4 HAL 路线

HAL 版按这个顺序实现：

1. `HAL_Init()`。
2. 配置系统时钟。
3. 配置 PC13 输出。
4. 配置 PA8 为 `GPIO_MODE_AF_PP`。
5. 填写 `htim1.Init`。
6. 调用 `HAL_TIM_PWM_Init()`。
7. 填写 `TIM_OC_InitTypeDef`。
8. 调用 `HAL_TIM_PWM_ConfigChannel()`。
9. 填写 `TIM_BreakDeadTimeConfigTypeDef`。
10. 调用 `HAL_TIMEx_ConfigBreakDeadTime()`。
11. 调用 `HAL_TIM_PWM_Start()`。

### 11.5 工程思维

高级定时器用于更危险的输出场景，所以多一道安全门是合理的。电机和功率驱动里，错误输出可能烧器件，不是只让 LED 不亮。

学习时要形成习惯：看到 TIM1/TIM8 输出，除了普通 PWM 配置，还要检查 BDTR 和 MOE。

### 11.6 常见工程陷阱

第一个陷阱是把 TIM1 当 TIM2 用，忘记 `MOE`。

第二个陷阱是测错引脚，TIM1_CH1 默认在 PA8。

第三个陷阱是只配置 BDTR，不调用 Start。

第四个陷阱是 `CCR1` 大于 `ARR+1`，占空比不再按直觉。

第五个陷阱是把 PC13 心跳当作 PA8 PWM 成功证据。

## 12. 运行现象

PA8 输出约 1kHz、30% 占空比 PWM。

用示波器观察时，周期约 1ms，高电平约 0.3ms。PC13 会周期性翻转，作为主循环心跳。

如果 PC13 正常闪但 PA8 没波形，优先检查 PA8 复用输出、TIM1 时钟、`CC1E` 和 `BDTR.MOE`。

## 13. 常见问题排查

### 13.1 PA8 完全无波形

检查 GPIOA 时钟、TIM1 时钟、PA8 是否配置成复用推挽。

然后重点检查 `TIM1->BDTR` 里的 `MOE`。TIM1 缺 `MOE` 是最典型无输出原因。

### 13.2 TIM1 内部计数正常但引脚不动

这通常说明时基没问题，输出路径有问题。

检查 `CCMR1.OC1M`、`CCER.CC1E`、`BDTR.MOE`、PA8 复用模式。

### 13.3 频率不对

重新计算：

```text
TIM1 计数频率 = 72MHz / (PSC + 1)
PWM 频率 = TIM1 计数频率 / (ARR + 1)
```

本课应为 1kHz。

### 13.4 占空比不对

检查 `CCR1 / (ARR + 1)`。本课是 300/1000，约 30%。

也要确认输出极性是高有效。

### 13.5 HAL 版无输出

确认调用了 `HAL_TIM_PWM_Start()`。

再查是否配置了 `HAL_TIMEx_ConfigBreakDeadTime()`，以及 PA8 是否为 `GPIO_MODE_AF_PP`。

## 14. 本课最核心的结论

- TIM1 是高级定时器，不只是普通 PWM 定时器。
- PA8 是 TIM1_CH1 默认复用输出脚。
- PWM 频率仍由 `PSC` 和 `ARR` 决定，占空比仍由 `CCR1` 决定。
- `CC1E` 打开通道输出，但 TIM1 还需要 `BDTR.MOE` 打开主输出。
- `BDTR` 里的死区、刹车、主输出使能面向功率驱动安全。
- HAL 的 `TIM_BreakDeadTimeConfigTypeDef` 是 BDTR 的软件封装。

## 15. 建议你现在怎么读这节课

先按 08 PWM 基础课的方式读 TIM1：`PSC`、`ARR`、`CCR1`、`OC1M`、`CC1E`。

然后专门停在 `BDTR.MOE`。问自己：如果这是 TIM2 会不会需要它？答案是不需要；正因为这是 TIM1，所以需要这道主输出门。

最后看 HAL 版，把 `TIM_BreakDeadTimeConfigTypeDef` 对应回 BDTR。

## 16. 扩展练习

1. 把 `CCR1` 改成 100、500、800，观察占空比变化。
2. 故意去掉 `TIM_BDTR_MOE`，观察 PA8 无输出。
3. 把 `ARR` 改成 2000-1，重新计算 PWM 频率。
4. 查 TIM1_CH1N 互补输出引脚，为后续互补 PWM 做准备。
5. 尝试设置非 0 `DeadTime`，理解它为什么用于半桥驱动。

## 17. 下一课预告

上一课：[13_timer_encoder](../13_timer_encoder/README.md)

下一课：[15_adc_polling](../15_adc_polling/README.md)

下一课会进入 ADC。学习重点会从“定时器如何处理时间和波形”转到“模拟电压如何被采样并转换成数字值”。

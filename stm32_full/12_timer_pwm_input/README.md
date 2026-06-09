# 12_timer_pwm_input - TIM PWM 输入模式

## 1. 本课到底在学什么

本课表面现象是：PA6 接入一个 PWM 信号后，程序能同时得到这个 PWM 的周期 `g_period_ticks` 和高电平时间 `g_high_ticks`，PC13 会周期性翻转，表示主循环还在持续读取测量值。

真正要学的是 TIM 的 PWM 输入模式。上一课普通输入捕获需要软件用两次上升沿捕获值相减来得到周期；本课让 TIM3 硬件把同一个输入信号分给两个捕获通道：CH1 捕获上升沿得到周期，CH2 捕获下降沿得到高电平时间，再用从模式 reset 让每个上升沿都把计数器清零。

这节课是从“测一个边沿时间戳”走向“直接测 PWM 参数”。后面遇到频率测量、占空比测量、舵机脉宽读取、遥控 PWM 输入时，这套 `周期 + 高电平` 的思路会反复出现。

## 2. 本课学习目标

学完本课，你应该能做到：

- 解释为什么本课只接 PA6 一个输入脚，却会同时用 TIM3_CH1 和 TIM3_CH2。
- 说明 `CCR1` 为什么保存周期，`CCR2` 为什么保存高电平时间。
- 区分 direct TI 和 indirect TI 的含义。
- 看懂 `CC1S = 01`、`CC2S = 10` 如何把同一个 TI1 输入分给两个通道。
- 解释 CH1 捕获上升沿、CH2 捕获下降沿的原因。
- 说明 `SMCR.TS = TI1FP1` 和 `SMCR.SMS = reset mode` 为什么能让周期直接可读。
- 根据 1MHz 计数频率把 tick 换算成微秒。
- 看懂 HAL 版 `TIM_IC_InitTypeDef` 和 `TIM_SlaveConfigTypeDef` 分别对应哪些寄存器。
- 能根据 `g_period_ticks`、`g_high_ticks` 判断输入 PWM 是否正确。

## 3. 本课目录结构

```text
12_timer_pwm_input/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

`reg/` 直接配置 TIM3 的 PWM 输入模式，主循环轮询 `CC1IF` 后读取 `CCR1` 和 `CCR2`。

`hal/` 使用 `HAL_TIM_IC_Init()`、`HAL_TIM_IC_ConfigChannel()`、`HAL_TIM_SlaveConfigSynchro()` 和 `HAL_TIM_ReadCapturedValue()` 完成同样测量。

本课代码不生成 PWM 信号，只负责测量 PA6 输入。你可以用前面 PWM 课程的 PA0 输出接到 PA6，也可以接外部信号发生器输出。

## 4. 实验硬件

本课使用：

- STM32F103C8T6 BluePill
- ST-Link 下载器
- PA6 输入 PWM 信号
- PC13 板载 LED
- 可选：另一根 PWM 输出线，例如前面课程 PA0 输出

推荐接法：

```text
PWM 输出源 ---- PA6
PWM 输出源 GND ---- STM32 GND
```

如果使用同一块板上的前面课程 PA0 PWM 输出作为信号源，则接：

```text
PA0 ---- 杜邦线 ---- PA6
```

如果使用外部信号发生器，必须共地。没有共同 GND，PA6 看到的电平没有可靠参考，测量值会乱跳。

## 5. 先建立一个最基本的脑图

本课按六层拆开看。

现象层：调试器里 `g_period_ticks` 表示 PWM 周期 tick，`g_high_ticks` 表示高电平 tick。输入 1kHz、50% PWM 时，理论上周期约 1000，高电平约 500。

物理/硬件层：PA6 是 TIM3_CH1 的输入脚。外部 PWM 只接这一根信号线，TIM3 内部会把 TI1 信号同时送到 CH1 和 CH2 两条捕获路径。

芯片模块层：GPIOA 配置 PA6 输入，TIM3 提供计数器、捕获通道和从模式复位，GPIOC 控制 PC13，HAL 版还用 SysTick 支撑 `HAL_Delay()`。

寄存器/bit 层：`PSC` 让 TIM3 以 1MHz 计数，`ARR=0xFFFF` 扩大测量范围，`CCMR1.CC1S` 和 `CC2S` 设置 direct/indirect 输入，`CCER` 设置捕获边沿和使能，`SMCR.TS/SMS` 设置 TI1 上升沿复位计数器。

C/CMSIS 层：寄存器版轮询 `TIM3->SR & TIM_SR_CC1IF`，读取 `TIM3->CCR1` 和 `TIM3->CCR2` 到 `volatile` 全局变量。

HAL/工程层：HAL 版用两个 `HAL_TIM_IC_ConfigChannel()` 分别配置 CH1 和 CH2，用 `HAL_TIM_SlaveConfigSynchro()` 配从模式 reset，用 `HAL_TIM_IC_Start()` 启动两个通道。

完整链路是：

1. 系统时钟配置到 72MHz。
2. PC13 配成输出，用作运行指示。
3. PA6 配成浮空输入，接待测 PWM。
4. TIM3 设置 `PSC = 72 - 1`，计数频率 1MHz。
5. TIM3 设置 `ARR = 0xFFFF`，计数范围 0 到 65535。
6. CH1 设置 direct TI，连接 TI1，捕获上升沿。
7. CH2 设置 indirect TI，也连接 TI1，捕获下降沿。
8. 从模式触发源选择 TI1FP1。
9. 从模式选择 reset mode，每个上升沿复位 `CNT`。
10. 下一个上升沿到来时，CH1 捕获周期到 `CCR1`。
11. 下降沿到来时，CH2 捕获高电平时间到 `CCR2`。
12. 软件读取 `CCR1` 和 `CCR2`，得到周期和高电平时间。
13. 占空比可以由 `g_high_ticks / g_period_ticks` 计算。

## 6. 核心名词解释

### 6.1 PWM 输入模式是什么

PWM 输入模式是定时器输入捕获的一种组合用法。

它属于 TIM 外设测量功能层。它用一个输入信号，同时配置两个捕获通道，一个测周期，一个测高电平或低电平时间。

本课中 PA6 的 TI1 信号被 CH1 和 CH2 同时使用。CH1 捕获上升沿，CH2 捕获下降沿，再配合 reset mode 得到直观的 `CCR1=周期`、`CCR2=高电平`。

如果不使用这种组合，只用普通输入捕获，通常还要软件记录多次边沿并自己判断高低电平。

### 6.2 `PA6` 是什么

PA6 是 GPIOA 的 6 号引脚。

它属于物理引脚层和 TIM3 输入层。本课 PA6 接入待测 PWM，作为 TIM3_CH1 的输入来源。

寄存器版把 PA6 配成浮空输入；HAL 版使用 `GPIO_MODE_INPUT` 和 `GPIO_NOPULL`。

如果 PA6 没有接 PWM，`CCR1` 和 `CCR2` 可能一直是 0 或保持旧值。

### 6.3 `TI1` 是什么

`TI1` 是 Timer Input 1，中文可叫定时器输入 1。

它属于 TIM3 内部输入信号路径层。对 TIM3_CH1 来说，PA6 上的电平经过输入路径进入 TI1。

本课所有测量都来自 TI1。CH1 直接连接 TI1，CH2 间接连接 TI1。

如果把 PWM 接到别的引脚，TI1 收不到信号，PWM 输入模式就没有测量对象。

### 6.4 `TIM3_CH1` 是什么

`TIM3_CH1` 是 TIM3 的通道 1。

它属于捕获通道层。本课让 CH1 直接连接 TI1，捕获上升沿。由于计数器在上升沿复位，下一次上升沿捕获到的 `CCR1` 就是完整周期。

代码中 CH1 对应 `CC1S`、`CC1E`、`CCR1`，HAL 版对应 `TIM_CHANNEL_1`。

### 6.5 `TIM3_CH2` 是什么

`TIM3_CH2` 是 TIM3 的通道 2。

它也属于捕获通道层。本课让 CH2 间接连接 TI1，而不是使用自己的 TI2 输入。CH2 捕获下降沿，所以 `CCR2` 保存从周期起点到下降沿的时间，也就是高电平持续时间。

代码中 CH2 对应 `CC2S`、`CC2E`、`CC2P`、`CCR2`，HAL 版对应 `TIM_CHANNEL_2`。

### 6.6 direct TI 是什么

direct TI 中文可以叫直接输入映射。

它属于定时器通道输入映射层。通道直接连接自己的输入线，比如 CH1 直接接 TI1。

本课 `CC1S = 01`，HAL 版 `ICSelection = TIM_ICSELECTION_DIRECTTI`，表示 CH1 直接使用 TI1。

如果 CH1 没有 direct 接 TI1，就不能正确捕获 PA6 上升沿。

### 6.7 indirect TI 是什么

indirect TI 中文可以叫间接输入映射。

它属于定时器通道交叉输入映射层。通道不使用自己的输入线，而是交叉使用另一个输入线。

本课 `CC2S = 10`，HAL 版 `ICSelection = TIM_ICSELECTION_INDIRECTTI`，表示 CH2 间接使用 TI1。

这就是一个 PA6 输入能同时被 CH1 和 CH2 捕获的关键。

### 6.8 `CCR1` 是什么

`CCR1` 是 Capture/Compare Register 1。

它属于 CH1 捕获寄存器层。本课中 `CCR1` 保存周期 tick。原因是每个 TI1 上升沿会复位计数器，下一次上升沿捕获到的值就是从上一个上升沿到本次上升沿的时间。

输入 1kHz PWM 时，`CCR1` 应接近 1000。

### 6.9 `CCR2` 是什么

`CCR2` 是 Capture/Compare Register 2。

它属于 CH2 捕获寄存器层。本课中 `CCR2` 保存高电平持续 tick。因为 CH2 捕获同一个 TI1 的下降沿，而计数器在上升沿从 0 开始，所以下降沿时间戳就是高电平宽度。

输入 1kHz、50% 占空比 PWM 时，`CCR2` 应接近 500。

### 6.10 `CCMR1.CC1S/CC2S` 是什么

`CC1S` 和 `CC2S` 是 Capture/Compare Selection 字段。

它们属于 TIM3 捕获/比较模式寄存器层。本课寄存器版：

```c
TIM3->CCMR1 = TIM_CCMR1_CC1S_0 | TIM_CCMR1_CC2S_1;
```

`CC1S = 01` 表示 CH1 direct 接 TI1。`CC2S = 10` 表示 CH2 indirect 接 TI1。

如果 `CC2S` 配错，`CCR2` 就不会保存高电平时间。

### 6.11 `CCER.CC1E/CC2E` 是什么

`CC1E` 和 `CC2E` 是通道捕获使能位。

它们属于 TIM3 通道使能层。本课两个通道都要捕获，所以都必须打开。

寄存器版：

```c
TIM3->CCER = TIM_CCER_CC1E | TIM_CCER_CC2E | TIM_CCER_CC2P;
```

如果只打开 CH1，周期可能有值，但高电平时间不会更新。

### 6.12 `CC2P` 是什么

`CC2P` 是通道 2 极性位。

它属于捕获边沿选择层。本课设置 `CC2P = 1`，让 CH2 捕获下降沿。CH1 默认捕获上升沿。

下降沿发生时，计数器从上升沿复位后已经走过的时间就是高电平宽度。

如果 CH2 也捕获上升沿，`CCR2` 不会表示高电平时间。

### 6.13 `SMCR.TS` 是什么

`TS` 是 Trigger Selection，中文叫触发源选择。

它属于 TIM3 从模式控制层。本课选择 TI1FP1 作为触发源，也就是经过滤波/极性处理后的 TI1 信号。

寄存器版用 `TIM_SMCR_TS_2 | TIM_SMCR_TS_0` 得到 `TS = 101`，HAL 版用 `TIM_TS_TI1FP1`。

如果触发源选错，计数器不会在 PA6 上升沿复位，`CCR1` 就不能直接表示周期。

### 6.14 `SMCR.SMS` 是什么

`SMS` 是 Slave Mode Selection，中文叫从模式选择。

它属于 TIM3 从模式行为层。本课选择 reset mode。触发源到来时，计数器被复位。

寄存器版用 `TIM_SMCR_SMS_2` 得到 reset mode，HAL 版用 `TIM_SLAVEMODE_RESET`。

如果不用 reset mode，`CCR1` 只是自由运行计数器上的时间戳，需要软件相减后才是周期。

### 6.15 `CC1IF` 是什么

`CC1IF` 是 Capture/Compare 1 Interrupt Flag。

它属于 TIM3 状态标志层。本课寄存器版主循环轮询它：

```c
if ((TIM3->SR & TIM_SR_CC1IF) != 0U) {
```

它表示 CH1 已经捕获到新的周期值。代码随后读取 `CCR1` 和 `CCR2`。

### 6.16 `HAL_TIM_SlaveConfigSynchro()` 是什么

这是 HAL 配置定时器从模式同步的接口。

它属于 HAL 从模式封装层。本课传入 `TIM_SlaveConfigTypeDef`，设置：

```c
slave.SlaveMode = TIM_SLAVEMODE_RESET;
slave.InputTrigger = TIM_TS_TI1FP1;
```

它对应寄存器版 `TIM3->SMCR` 的 `SMS` 和 `TS` 字段。

### 6.17 `HAL_TIM_ReadCapturedValue()` 是什么

这是 HAL 读取捕获寄存器的接口。

它属于 HAL 寄存器读取封装层。本课主循环分别读取：

```c
HAL_TIM_ReadCapturedValue(&htim3, TIM_CHANNEL_1)
HAL_TIM_ReadCapturedValue(&htim3, TIM_CHANNEL_2)
```

前者读取 `CCR1` 周期，后者读取 `CCR2` 高电平时间。

## 7. 寄存器版代码逐步讲解

### 7.1 系统时钟和 PC13 初始化

`system_clock_72mhz_init()` 配置 HSE 8MHz、PLL x9、SYSCLK 72MHz。`pc13_led_init()` 打开 GPIOC，把 PC13 配成推挽输出，初始写高电平让 LED 熄灭。

这部分不是 PWM 输入测量本身，但给 TIM3 的 1MHz 计数计算和运行指示提供基础。

### 7.2 `g_period_ticks` 和 `g_high_ticks`

代码：

```c
static volatile uint32_t g_period_ticks;
static volatile uint32_t g_high_ticks;
```

`g_period_ticks` 保存周期 tick，`g_high_ticks` 保存高电平 tick。它们声明为 `volatile`，方便调试器和主循环观察最新硬件读取结果。

### 7.3 打开 GPIOA 和 TIM3 时钟

代码：

```c
RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;
RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
```

PA6 属于 GPIOA，TIM3 挂在 APB1。两个时钟都要打开，否则引脚模式和定时器寄存器配置都不会可靠生效。

### 7.4 PA6 配成浮空输入

代码：

```c
GPIOA->CRL &= ~(GPIO_CRL_MODE6 | GPIO_CRL_CNF6);
GPIOA->CRL |= GPIO_CRL_CNF6_0;
```

`MODE6 = 00` 表示输入，`CNF6 = 01` 表示浮空输入。PA6 的电平由外部 PWM 源驱动。

### 7.5 TIM3 计数时间轴

代码：

```c
TIM3->PSC = 72U - 1U;
TIM3->ARR = 0xFFFFU;
```

TIM3 输入按 72MHz 计算，除以 72 得到 1MHz。1 tick 是 1us。`ARR=0xFFFF` 让最大计数范围约 65.536ms。

### 7.6 配置 CH1 direct、CH2 indirect

代码：

```c
TIM3->CCMR1 = TIM_CCMR1_CC1S_0 | TIM_CCMR1_CC2S_1;
```

`CC1S=01`：CH1 direct 接 TI1。`CC2S=10`：CH2 indirect 接 TI1。

这一步是 PWM 输入模式的核心。一个 PA6/TI1 输入在 TIM3 内部分给两个捕获通道。

### 7.7 配置捕获边沿和使能

代码：

```c
TIM3->CCER = TIM_CCER_CC1E | TIM_CCER_CC2E | TIM_CCER_CC2P;
```

`CC1E` 打开 CH1 捕获，默认上升沿。`CC2E` 打开 CH2 捕获。`CC2P` 让 CH2 捕获下降沿。

所以 CH1 得到周期边界，CH2 得到高电平结束点。

### 7.8 配置从模式 reset

代码：

```c
TIM3->SMCR = TIM_SMCR_TS_2 | TIM_SMCR_TS_0 | TIM_SMCR_SMS_2;
```

`TS=101` 选择 TI1FP1 作为触发源，`SMS=100` 选择 reset mode。

硬件后果是：每个 TI1 上升沿到来时，计数器被复位。这样下一次上升沿捕获到的 CH1 值就是完整周期。

### 7.9 启动 TIM3

代码：

```c
TIM3->CR1 = TIM_CR1_CEN;
```

`CEN` 启动计数器。没有计数器运行，捕获值没有时间意义。

### 7.10 主循环轮询 `CC1IF`

代码：

```c
if ((TIM3->SR & TIM_SR_CC1IF) != 0U) {
```

本课寄存器版不用中断，而是轮询 CH1 捕获标志。CH1 捕获新周期后，说明 `CCR1` 和 `CCR2` 都可以读取。

### 7.11 读取周期和高电平

代码：

```c
g_period_ticks = TIM3->CCR1;
g_high_ticks = TIM3->CCR2;
```

`CCR1` 是周期，`CCR2` 是高电平时间。因为 TIM3 是 1MHz 计数，所以这两个值的单位都是微秒。

占空比可以计算为：

```text
duty = g_high_ticks / g_period_ticks
```

### 7.12 清 `CC1IF` 并翻转 PC13

代码：

```c
TIM3->SR &= ~TIM_SR_CC1IF;
pc13_toggle();
```

清掉 CH1 捕获标志，等待下一次周期捕获。PC13 翻转只是运行指示，不参与 PWM 测量。

## 8. HAL 版代码逐步讲解

### 8.1 `HAL_Init()` 和系统时钟

HAL 版先调用 `HAL_Init()`，再用 `RCC_OscInitTypeDef` 和 `RCC_ClkInitTypeDef` 配置 HSE、PLL、AHB、APB。

目标仍然是 72MHz 系统时钟，让 TIM3 的 `Prescaler=72-1` 对应 1MHz。

### 8.2 HAL 配置 PC13

`pc13_led_init()` 使用 `GPIO_InitTypeDef` 把 PC13 配成 `GPIO_MODE_OUTPUT_PP`，初始写 `GPIO_PIN_SET`。

这对应寄存器版 GPIOC 时钟、`CRH` 和 `BSRR`。

### 8.3 HAL 配置 PA6 输入

代码：

```c
gpio.Pin = GPIO_PIN_6;
gpio.Mode = GPIO_MODE_INPUT;
gpio.Pull = GPIO_NOPULL;
HAL_GPIO_Init(GPIOA, &gpio);
```

PA6 作为 TIM3 输入脚，接外部 PWM。本课不用内部上拉下拉，因为信号源会主动驱动高低电平。

### 8.4 `HAL_TIM_IC_Init()` 配 TIM3 时基

代码：

```c
htim3.Instance = TIM3;
htim3.Init.Prescaler = 72U - 1U;
htim3.Init.Period = 0xFFFFU;
HAL_TIM_IC_Init(&htim3);
```

这对应寄存器版 `TIM3->PSC`、`TIM3->ARR` 和基础计数配置。

### 8.5 CH1 配置 direct 上升沿

代码：

```c
ic.ICPolarity = TIM_INPUTCHANNELPOLARITY_RISING;
ic.ICSelection = TIM_ICSELECTION_DIRECTTI;
HAL_TIM_IC_ConfigChannel(&htim3, &ic, TIM_CHANNEL_1);
```

这对应 CH1 direct 接 TI1，捕获上升沿，结果进入 `CCR1`。

### 8.6 CH2 配置 indirect 下降沿

代码：

```c
ic.ICPolarity = TIM_INPUTCHANNELPOLARITY_FALLING;
ic.ICSelection = TIM_ICSELECTION_INDIRECTTI;
HAL_TIM_IC_ConfigChannel(&htim3, &ic, TIM_CHANNEL_2);
```

这对应 CH2 indirect 接 TI1，捕获下降沿，结果进入 `CCR2`。

### 8.7 `TIM_SlaveConfigTypeDef` 配 reset mode

代码：

```c
slave.SlaveMode = TIM_SLAVEMODE_RESET;
slave.InputTrigger = TIM_TS_TI1FP1;
HAL_TIM_SlaveConfigSynchro(&htim3, &slave);
```

这对应寄存器版 `SMCR.SMS` 和 `SMCR.TS`。TI1FP1 上升沿作为触发源，触发时复位计数器。

### 8.8 启动两个捕获通道

代码：

```c
HAL_TIM_IC_Start(&htim3, TIM_CHANNEL_1);
HAL_TIM_IC_Start(&htim3, TIM_CHANNEL_2);
```

两个通道都要启动。只启动 CH1 只能得到周期；只启动 CH2 高电平测量也不完整。

本课 HAL 版使用轮询读取，不使用 `_IT` 中断启动。

### 8.9 主循环读取捕获值

代码：

```c
g_period_ticks = HAL_TIM_ReadCapturedValue(&htim3, TIM_CHANNEL_1);
g_high_ticks = HAL_TIM_ReadCapturedValue(&htim3, TIM_CHANNEL_2);
```

这分别读取 `CCR1` 和 `CCR2`。HAL 版没有检查 `CC1IF`，而是每 500ms 读取一次当前捕获寄存器值。

### 8.10 `HAL_Delay()` 和 `SysTick_Handler()`

主循环用：

```c
HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
HAL_Delay(500);
```

`HAL_Delay()` 依赖 `SysTick_Handler()` 中的 `HAL_IncTick()`。PC13 翻转只是运行指示，和捕获事件频率不是同一回事。

## 9. 两个版本真正应该怎么学

寄存器版抓三件事：

```text
CCMR1：一个 TI1 输入分给 CH1/CH2
CCER：CH1 上升沿，CH2 下降沿
SMCR：上升沿复位 CNT，让 CCR1/CCR2 直接可读
```

HAL 版抓三个结构体：

```text
TIM_HandleTypeDef：TIM3 时基
TIM_IC_InitTypeDef：CH1/CH2 捕获映射和边沿
TIM_SlaveConfigTypeDef：TI1FP1 reset mode
```

本课和 11 课最大的差别是：11 课用软件相减得到周期；12 课用 reset mode 让硬件把周期和高电平时间直接放进两个 CCR。

## 10. 检验问题清单

### 10.1 为什么一个 PA6 可以测周期和高电平？

答：PA6 进入 TIM3 的 TI1 后，CH1 direct 接 TI1，CH2 indirect 也接 TI1。一个输入信号在定时器内部被分给两个捕获通道。

### 10.2 `CCR1` 保存什么？

答：`CCR1` 保存周期 tick。因为计数器在上升沿复位，下一次上升沿捕获到的计数值就是完整周期。

### 10.3 `CCR2` 保存什么？

答：`CCR2` 保存高电平时间 tick。计数器从上升沿开始，下降沿到来时 CH2 捕获当前计数值，也就是高电平持续时间。

### 10.4 如果不配置 reset mode 会怎样？

答：`CCR1` 和 `CCR2` 只是自由运行计数器上的时间戳，不能直接表示周期和高电平时间，需要软件相减。

### 10.5 CH2 为什么要配置 falling？

答：高电平从上升沿开始，到下降沿结束。CH2 捕获下降沿，才能得到高电平结束时刻。

### 10.6 `TIM_TS_TI1FP1` 对应寄存器版什么？

答：对应 `SMCR.TS = 101`，选择 TI1FP1 作为触发源。

### 10.7 HAL 版为什么要启动两个通道？

答：CH1 负责周期，CH2 负责高电平。两个通道都启动后，`CCR1` 和 `CCR2` 才都会更新。

### 10.8 输入 2kHz、25% PWM 时读数大约是多少？

答：2kHz 周期是 500us。25% 高电平约 125us。所以 `g_period_ticks` 约 500，`g_high_ticks` 约 125。

## 11. 工程实现步骤

### 11.1 需求分析

需求是测量一个外部 PWM 的周期和高电平时间。

这要求 PA6 有稳定 PWM 输入，TIM3 以 1us 分辨率计数，CH1 捕获上升沿周期，CH2 捕获下降沿高电平，reset mode 把测量窗口对齐到每个周期起点。

### 11.2 硬件核查

先确认 PA6 确实有 PWM 输入。如果使用外部信号源，必须共地。如果使用前面课程 PA0 输出，则确认 PA0 到 PA6 的杜邦线接好。

推荐先用示波器看 PA6，确认频率和占空比，再看软件变量。

### 11.3 寄存器路线

寄存器版按这个顺序实现：

1. 配置系统时钟。
2. 配置 PC13 输出。
3. 打开 GPIOA 和 TIM3 时钟。
4. PA6 配成浮空输入。
5. TIM3 `PSC=72-1`、`ARR=0xFFFF`。
6. `CC1S=01`，CH1 direct 接 TI1。
7. `CC2S=10`，CH2 indirect 接 TI1。
8. `CC1E=1`，CH1 捕获上升沿。
9. `CC2E=1`、`CC2P=1`，CH2 捕获下降沿。
10. `TS=TI1FP1`，`SMS=reset mode`。
11. `CEN=1` 启动 TIM3。
12. 轮询 `CC1IF` 后读 `CCR1/CCR2`。

### 11.4 HAL 路线

HAL 版按这个顺序实现：

1. `HAL_Init()`。
2. 配置系统时钟。
3. 配置 PC13 输出。
4. 配置 PA6 输入无上下拉。
5. `HAL_TIM_IC_Init()` 配 TIM3 时基。
6. CH1 用 `RISING + DIRECTTI`。
7. CH2 用 `FALLING + INDIRECTTI`。
8. `HAL_TIM_SlaveConfigSynchro()` 配 `RESET + TI1FP1`。
9. `HAL_TIM_IC_Start()` 启动 CH1。
10. `HAL_TIM_IC_Start()` 启动 CH2。
11. 主循环读 CH1/CH2 捕获值。

### 11.5 工程思维

PWM 输入模式的工程价值是减少软件计算。硬件把周期和高电平时间分别放到两个寄存器，软件只需要读取并计算占空比。

这比普通输入捕获更适合“持续测 PWM 参数”。普通输入捕获更通用，PWM 输入模式更专门。

### 11.6 常见工程陷阱

第一个陷阱是忘记输入 PWM 或忘记共地。

第二个陷阱是 CH2 没配 indirect，导致高电平时间不对。

第三个陷阱是没有 reset mode，导致 `CCR1` 不是周期。

第四个陷阱是只启动一个通道。

第五个陷阱是把 PC13 翻转当成测量频率。PC13 只是运行指示，真实测量看 `g_period_ticks` 和 `g_high_ticks`。

## 12. 运行现象

PA6 有 PWM 输入时，`g_period_ticks` 会保存周期，`g_high_ticks` 会保存高电平时间。

如果输入 1kHz、50% 占空比 PWM，理论值是：

```text
g_period_ticks ≈ 1000
g_high_ticks ≈ 500
```

寄存器版在每次 CH1 捕获后翻转 PC13。HAL 版每 500ms 读取一次捕获值并翻转 PC13。PC13 只是说明程序在运行，不是 PWM 输入本身的精确指示。

## 13. 常见问题排查

### 13.1 两个值一直是 0

先查 PA6 是否真的有 PWM 输入。没有输入信号，捕获寄存器不会更新。

再查 GPIOA 时钟、PA6 输入模式、TIM3 时钟、两个捕获通道是否启动。

### 13.2 周期值乱跳

检查信号源幅度、共地、线是否过长。外部信号噪声大时，可以增加输入滤波。

如果输入频率太低，周期超过 65536us，也会超出当前 16 位计数范围。

### 13.3 高电平时间不对

重点查 CH2：它必须 indirect 接 TI1，并且捕获下降沿。

寄存器版看 `CC2S=10` 和 `CC2P=1`；HAL 版看 `TIM_ICSELECTION_INDIRECTTI` 和 `TIM_INPUTCHANNELPOLARITY_FALLING`。

### 13.4 `CCR1` 不是周期

检查 `SMCR` 或 HAL slave config。必须选择 TI1FP1 作为触发源，并启用 reset mode。

没有 reset mode 时，`CCR1` 是时间戳，不是周期。

### 13.5 HAL 版只读到一个通道

确认 CH1 和 CH2 都调用了 `HAL_TIM_IC_Start()`。

还要确认第二次配置 `TIM_IC_InitTypeDef` 时确实把 `ICPolarity` 改成 falling，把 `ICSelection` 改成 indirect。

## 14. 本课最核心的结论

- PWM 输入模式用一个输入信号同时驱动两个捕获通道。
- CH1 direct 接 TI1 并捕获上升沿，得到周期。
- CH2 indirect 接 TI1 并捕获下降沿，得到高电平时间。
- reset mode 让每个上升沿复位计数器，使 `CCR1/CCR2` 直接可读。
- 1MHz 计数时，tick 数就是微秒数。
- HAL 的 `TIM_SlaveConfigTypeDef` 本质上配置的是 `SMCR.TS` 和 `SMCR.SMS`。

## 15. 建议你现在怎么读这节课

先画 PA6 输入线，然后画 TIM3 内部两条路径：TI1 到 CH1，TI1 到 CH2。

再把一个 PWM 周期拆成两个边沿：上升沿复位并作为周期边界，下降沿表示高电平结束。

最后回到代码，只看三个寄存器组：`CCMR1` 分配输入，`CCER` 选择边沿，`SMCR` 复位计数器。

## 16. 扩展练习

1. 输入 500Hz PWM，观察 `g_period_ticks` 是否约 2000。
2. 输入 2kHz PWM，观察 `g_period_ticks` 是否约 500。
3. 改变占空比，观察 `g_high_ticks` 是否随之变化。
4. 去掉 reset mode，观察 `CCR1/CCR2` 为什么不再直观。
5. 增加输入滤波，观察噪声信号下读数是否更稳定。

## 17. 下一课预告

上一课：[11_input_capture](../11_input_capture/README.md)

下一课：[13_timer_encoder](../13_timer_encoder/README.md)

下一课会进入编码器模式。那时定时器不再只测一个 PWM 的周期，而是根据两路正交信号自动判断方向并计数。

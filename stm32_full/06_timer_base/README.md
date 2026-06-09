# 第 6 课：定时器基础

## 1. 本课到底在学什么

这节课表面上是在做：

- 配置 `TIM2` 每 1 秒产生一次更新中断
- 在 `TIM2_IRQHandler()` 或 HAL 回调里翻转 `PC13`
- 让板载 LED 每 1 秒改变一次亮灭状态

真正学习的是 STM32 通用定时器的基础定时链路：

```text
TIM2 输入时钟 = 72MHz
  -> PSC = 7200 - 1，把计数频率分到 10kHz
  -> CNT 从 0 向上计数
  -> ARR = 10000 - 1，计满 10000 个 tick
  -> 产生更新事件
  -> SR.UIF 置位
  -> DIER.UIE 允许更新事件发中断
  -> NVIC 放行 TIM2_IRQn
  -> 进入 TIM2_IRQHandler()
  -> 清 UIF 并翻转 PC13
```

上一课 `SysTick` 是 Cortex-M3 内核自带的系统节拍定时器。本课开始进入 STM32 外设定时器。后面的输出比较、PWM、输入捕获、编码器接口，本质都建立在 TIM 能稳定计数、产生事件、驱动通道或中断的基础上。

## 2. 本课学习目标

学完本课，你应该能回答：

1. `TIM2` 为什么是 STM32 外设，而不是 Cortex-M 内核组件？
2. 为什么 `TIM2` 挂在 APB1 上，但本课定时器输入时钟仍然是 72MHz？
3. `PSC = 7200 - 1` 和 `ARR = 10000 - 1` 为什么刚好得到 1 秒？
4. `CNT`、`PSC`、`ARR`、`SR.UIF`、`DIER.UIE`、`CR1.CEN` 分别控制哪一层行为？
5. 为什么只产生 `UIF` 还不够，必须打开 `UIE` 和 NVIC？
6. 为什么 `TIM2_IRQHandler()` 里必须先判断并清除 `UIF`？
7. HAL 版的 `Prescaler`、`Period`、`HAL_TIM_Base_Start_IT()` 分别对应哪些寄存器动作？
8. `HAL_TIM_PeriodElapsedCallback()` 是谁调用的，为什么业务代码写在这里？

## 3. 本课目录结构

```text
06_timer_base/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

`reg/` 直接配置 TIM2 的基础计数、更新中断、NVIC 和中断服务函数。

`hal/` 使用 `TIM_HandleTypeDef`、`HAL_TIM_Base_Init()`、`HAL_TIM_Base_Start_IT()` 和 HAL 定时器回调实现同样的 1 秒翻转。

## 4. 实验硬件

- 开发板：STM32F103C8T6 BluePill
- 下载器：ST-Link
- 观察对象：板载 `PC13` LED
- 时钟假设：HSE 8MHz，PLL 后 `SYSCLK/HCLK = 72MHz`
- 总线配置：APB1 = 36MHz，APB2 = 72MHz

本课没有额外接线。PC13 LED 只是用来观察 TIM2 中断是否按 1 秒节奏发生。常见 BluePill 的 PC13 LED 低电平点亮，所以每次翻转会在亮和灭之间切换。

## 5. 先建立一个最基本的脑图

本课完整链路如下：

```text
system_clock_72mhz_init()
  -> SYSCLK/HCLK = 72MHz
  -> APB1 = 36MHz，但 APB1 分频不为 1
  -> TIM2 定时器时钟 = 2 x PCLK1 = 72MHz
  -> led_pc13_init()
  -> GPIOC 时钟打开，PC13 配成输出
  -> tim2_base_init()
  -> RCC->APB1ENR 打开 TIM2 时钟
  -> TIM2->PSC = 7200 - 1，计数频率变成 10kHz
  -> TIM2->ARR = 10000 - 1，10000 个计数产生一次更新
  -> 清 TIM2->SR.UIF，避免旧标志误触发
  -> TIM2->DIER.UIE = 1，允许更新中断
  -> NVIC_EnableIRQ(TIM2_IRQn)，CPU 放行 TIM2 中断
  -> TIM2->CR1.CEN = 1，计数器开始跑
  -> CNT 溢出产生更新事件，UIF 置位
  -> TIM2_IRQHandler() 清 UIF 并翻转 PC13
```

这条链路里最关键的是三层开关：

1. `RCC->APB1ENR.TIM2EN`：TIM2 外设有没有时钟。
2. `TIM2->DIER.UIE`：TIM2 更新事件是否允许发中断请求。
3. `NVIC_EnableIRQ(TIM2_IRQn)`：CPU 是否接收 TIM2 这个中断。

少任何一层，LED 都不会按 1 秒中断节奏翻转。

## 6. 先认识本课里出现的核心名词

### 6.1 `TIM2` 是什么

`TIM2` 全称可以理解为：

- Timer 2

中文通常叫：

- 通用定时器 2

它属于 STM32F103 的片上外设，不属于 Cortex-M 内核。上一课的 `SysTick` 是内核定时器，而 `TIM2` 是 STM32 厂商外设，挂在 APB1 总线上。

它的作用是：

- 按输入时钟驱动 `CNT` 计数。
- 通过 `PSC` 降低计数频率。
- 通过 `ARR` 决定更新周期。
- 产生更新事件、输出比较、PWM、输入捕获等功能。

在本课里，TIM2 只用最基础能力：计数到 `ARR` 后产生更新中断。如果 TIM2 时钟没打开，后面对 `TIM2->PSC`、`TIM2->ARR` 的配置都不会可靠驱动硬件。

### 6.2 `APB1` 是什么

`APB1` 全称是：

- Advanced Peripheral Bus 1

中文通常叫：

- 高级外设总线 1

它属于 STM32 的总线/时钟树层。很多低速或中速外设挂在 APB1 上，例如 TIM2、TIM3、USART2、I2C 等。

本课时钟配置中：

```text
HCLK = 72MHz
APB1 = HCLK / 2 = 36MHz
```

但是 STM32F1 有一个定时器时钟规则：当 APB 预分频不为 1 时，挂在这条 APB 上的定时器输入时钟会变成 `2 x PCLK`。

所以本课：

```text
PCLK1 = 36MHz
TIM2CLK = 2 x 36MHz = 72MHz
```

如果你忽略这个规则，用 36MHz 去算 PSC/ARR，定时周期会错一倍。

### 6.3 `RCC->APB1ENR` 是什么

`APB1ENR` 全称可以理解为：

- APB1 peripheral clock enable register

中文通常叫：

- APB1 外设时钟使能寄存器

它属于 RCC 模块，控制 APB1 总线上各外设的时钟开关。

本课代码写：

```c
RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
```

意思是打开 TIM2 外设时钟。它处于第 5 章链路中“TIM2 寄存器配置生效”之前。

如果漏掉这一步，TIM2 外设没有时钟，计数器不会正常工作，LED 不会被 TIM2 中断翻转。

### 6.4 `PSC` 是什么

`PSC` 全称是：

- Prescaler

中文通常叫：

- 预分频寄存器

它属于 TIM2 外设寄存器组，控制定时器输入时钟先被除以多少再送给计数器。

定时器里的分频规则是：

```text
计数频率 = TIM 输入时钟 / (PSC + 1)
```

本课写：

```c
TIM2->PSC = 7200U - 1U;
```

因为 TIM2 输入时钟是 72MHz，所以：

```text
72MHz / 7200 = 10kHz
```

如果 `PSC` 写太小，计数太快，LED 翻转更快。如果写太大，计数太慢，LED 翻转更慢。

### 6.5 `ARR` 是什么

`ARR` 全称是：

- Auto-Reload Register

中文通常叫：

- 自动重装载寄存器

它属于 TIM2 外设寄存器组，决定计数器数到哪里产生更新事件，并回到起点继续计数。

本课写：

```c
TIM2->ARR = 10000U - 1U;
```

因为 PSC 后的计数频率是 10kHz，也就是每秒 10000 个计数。`ARR = 9999` 时，计数覆盖 10000 个 tick，正好 1 秒产生一次更新事件。

如果 `ARR` 改成 `5000 - 1`，更新周期变成 0.5 秒，LED 翻转速度变快。

### 6.6 `CNT` 是什么

`CNT` 全称是：

- Counter

中文通常叫：

- 计数器当前值

它属于 TIM2 外设寄存器组，是定时器正在数的那个值。

本课代码没有显式写 `TIM2->CNT`，但 TIM2 启动后硬件会自动让 `CNT` 从 0 向上递增。`CNT` 数到 `ARR` 后，产生更新事件并重新开始下一轮。

你可以在调试器里观察 `TIM2->CNT`。如果 `CNT` 不动，优先查 TIM2 时钟和 `CR1.CEN`。

### 6.7 `更新事件` 是什么

更新事件英文通常叫：

- Update Event

中文通常叫：

- 定时器更新事件

它属于 TIM 外设内部事件层。向上计数时，通常可以理解为 `CNT` 到达 `ARR` 并溢出回到 0 时产生一次更新事件。

本课用更新事件作为“1 秒到了”的硬件信号。更新事件发生后，硬件会置位 `SR.UIF`。如果 `DIER.UIE` 也打开，就会向 NVIC 发出中断请求。

如果只产生更新事件但不开中断，你可以轮询 `UIF`，但 CPU 不会自动进入 `TIM2_IRQHandler()`。

### 6.8 `TIM2->SR` 和 `UIF` 是什么

`SR` 全称是：

- Status Register

中文通常叫：

- 状态寄存器

`UIF` 全称可以理解为：

- Update Interrupt Flag

中文通常叫：

- 更新中断标志

它们属于 TIM2 外设状态层。更新事件发生时，硬件把 `TIM2->SR` 里的 `UIF` 置 1，告诉软件“更新事件发生过”。

本课中断函数先判断：

```c
if ((TIM2->SR & TIM_SR_UIF) != 0U) {
```

确认这次进入中断确实和更新事件有关。处理后再清除：

```c
TIM2->SR &= ~TIM_SR_UIF;
```

如果不清 `UIF`，CPU 退出中断后会发现标志还在，可能马上再次进入中断，表现为程序像被中断粘住。

### 6.9 `TIM2->DIER` 和 `UIE` 是什么

`DIER` 全称是：

- DMA/Interrupt Enable Register

中文通常叫：

- DMA/中断使能寄存器

`UIE` 全称可以理解为：

- Update Interrupt Enable

中文通常叫：

- 更新中断使能位

它们属于 TIM2 外设中断控制层。`UIF` 是“事件发生了”，`UIE` 是“这个事件允许申请中断”。

本课写：

```c
TIM2->DIER |= TIM_DIER_UIE;
```

如果不开 `UIE`，`UIF` 仍然可能置位，但不会触发 `TIM2_IRQHandler()`。

### 6.10 `TIM2_IRQn` 和 NVIC 是什么

`TIM2_IRQn` 是 TIM2 在 Cortex-M NVIC 中的中断编号。

`NVIC` 全称是：

- Nested Vectored Interrupt Controller

中文通常叫：

- 嵌套向量中断控制器

NVIC 属于 Cortex-M 内核中断控制层。外设内部开了 `UIE` 还不够，CPU 这边也要允许 TIM2 中断进入。

本课写：

```c
NVIC_SetPriority(TIM2_IRQn, 1U);
NVIC_EnableIRQ(TIM2_IRQn);
```

如果漏掉 `NVIC_EnableIRQ()`，TIM2 外设内部可能已经发出中断请求，但 CPU 不会跳进 `TIM2_IRQHandler()`。

### 6.11 `TIM2->CR1` 和 `CEN` 是什么

`CR1` 全称是：

- Control Register 1

中文通常叫：

- 控制寄存器 1

`CEN` 全称可以理解为：

- Counter Enable

中文通常叫：

- 计数器使能位

它们属于 TIM2 外设控制层。本课最后写：

```c
TIM2->CR1 |= TIM_CR1_CEN;
```

这一步才真正启动 TIM2 计数器。前面配置 PSC、ARR、UIE、NVIC 都是在铺路；没有 `CEN`，`CNT` 不会开始跑。

### 6.12 `TIM2_IRQHandler()` 是什么

`TIM2_IRQHandler()` 是 TIM2 的中断服务函数。

它属于 Cortex-M 中断入口层。启动文件中的中断向量表会把 `TIM2_IRQn` 对应到这个函数名。

本课在里面做三件事：

1. 判断 `UIF` 是否置位。
2. 清除 `UIF`。
3. 翻转 PC13 LED。

如果函数名写错，NVIC 即使收到 TIM2 中断，也进不到你的处理逻辑。

### 6.13 `TIM_HandleTypeDef` 是什么

`TIM_HandleTypeDef` 是 HAL 定义的定时器句柄结构体。

它属于 HAL 软件封装层，用来描述“我要操作哪个 TIM 外设，以及这个外设当前配置和状态是什么”。

本课 HAL 版定义：

```c
static TIM_HandleTypeDef htim2;
```

然后设置：

```c
htim2.Instance = TIM2;
```

这表示这个句柄绑定 TIM2。后面的 `HAL_TIM_Base_Init(&htim2)` 和 `HAL_TIM_Base_Start_IT(&htim2)` 都会通过这个句柄找到 TIM2 寄存器。

### 6.14 `HAL_TIM_PeriodElapsedCallback()` 是什么

`HAL_TIM_PeriodElapsedCallback()` 是 HAL 定时器周期到达回调函数。

它属于 HAL 回调层，不是你在主循环里主动调用的普通函数。TIM2 更新中断进入 `TIM2_IRQHandler()` 后，代码调用 `HAL_TIM_IRQHandler(&htim2)`，HAL 在里面检查标志并清标志，然后在合适时机调用这个回调。

本课在回调里判断：

```c
if (htim->Instance == TIM2) {
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
}
```

这个判断很重要，因为多个定时器都可能共用同一个回调函数。没有判断来源，后续工程里很容易把不同定时器事件混在一起。

## 7. 寄存器版代码逐步讲解

寄存器版在 [reg/src/main.c](reg/src/main.c)。

### 7.1 先看完整逻辑

寄存器版主流程是：

```c
int main(void)
{
    system_clock_72mhz_init();
    led_pc13_init();
    tim2_base_init();

    while (1) {
    }
}
```

主循环为空，不代表程序没事做。LED 翻转完全由 TIM2 更新中断驱动。

顺序不能乱：先配置系统时钟，因为 PSC/ARR 计算依赖 TIM2 输入时钟；再配置 PC13，因为中断里要翻转 LED；最后配置并启动 TIM2。

### 7.2 `system_clock_72mhz_init()` 为什么影响 TIM2

本课 TIM2 周期计算依赖这个时钟事实：

```text
HCLK = 72MHz
PCLK1 = 36MHz
APB1 prescaler = /2
TIM2CLK = 2 x PCLK1 = 72MHz
```

`system_clock_72mhz_init()` 设置 HSE、PLL、AHB/APB 分频。这里最容易漏掉的是 APB1 定时器倍频规则。

如果你按 PCLK1=36MHz 计算，而实际 TIM2CLK=72MHz，那么你以为是 1 秒的配置会变成 0.5 秒。

### 7.3 `led_pc13_init()` 为什么仍然是必要前置

本课的主角是 TIM2，但现象输出仍然靠 PC13。

`led_pc13_init()` 打开 GPIOC 时钟，把 PC13 配成通用推挽输出，并初始置高让 LED 熄灭。

如果 PC13 没初始化，TIM2 中断可能已经按 1 秒进入，但 LED 不会按预期显示。排错时要把“TIM2 没中断”和“LED 输出链路坏了”区分开。

### 7.4 `tim2_base_init()` 第一步：打开 TIM2 时钟

代码是：

```c
RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
```

TIM2 挂在 APB1 上，所以时钟使能位在 `RCC->APB1ENR`，不是 `APB2ENR`。

这一步执行后，TIM2 外设才获得时钟。后面写 `TIM2->PSC`、`TIM2->ARR`、`TIM2->DIER` 才能让硬件真正按配置工作。

如果忘记这一步，`CNT` 不会正常计数，LED 不会被 TIM2 中断翻转。

### 7.5 第二步：配置 `PSC`

代码是：

```c
TIM2->PSC = 7200U - 1U;
```

TIM2 输入时钟是 72MHz。定时器预分频公式是：

```text
计数频率 = TIM2CLK / (PSC + 1)
```

写 `7200 - 1` 后：

```text
72,000,000 / 7200 = 10,000 Hz
```

也就是 `CNT` 每 0.1ms 增加 1。

如果写成 `7200`，实际分频是 7201，周期会有轻微误差。定时器寄存器里这类 `+1` 规则要特别敏感。

### 7.6 第三步：配置 `ARR`

代码是：

```c
TIM2->ARR = 10000U - 1U;
```

`ARR` 决定一轮计数有多少个 tick。前面 PSC 后计数频率是 10kHz，也就是 1 秒 10000 个 tick。

写 `10000 - 1` 后，计数器从 0 数到 9999，一共 10000 个计数，正好 1 秒产生一次更新。

如果想 500ms 更新一次，可以把 ARR 改成 `5000 - 1`，因为 10kHz 下 5000 个 tick 是 0.5 秒。

### 7.7 第四步：启动前先清 `UIF`

代码是：

```c
TIM2->SR &= ~TIM_SR_UIF;
```

`UIF` 是更新标志。启动前先清它，是为了避免旧标志残留。

如果启动前 `UIF` 已经是 1，一旦你打开中断，CPU 可能马上进入一次 `TIM2_IRQHandler()`，让 LED 一上电就翻转一次。这会让现象不够干净，也容易误判定时器周期。

### 7.8 第五步：打开更新中断 `UIE`

代码是：

```c
TIM2->DIER |= TIM_DIER_UIE;
```

这一步打开的是 TIM2 外设内部的更新中断请求。它不是 NVIC 总开关。

更新事件发生时，硬件会置 `UIF`。只有 `UIE = 1`，这个更新事件才会向 NVIC 申请中断。

如果只开 NVIC，不开 `UIE`，CPU 仍然不会进入 `TIM2_IRQHandler()`。

### 7.9 第六步：配置 NVIC

代码是：

```c
NVIC_SetPriority(TIM2_IRQn, 1U);
NVIC_EnableIRQ(TIM2_IRQn);
```

`NVIC_SetPriority()` 设置 TIM2 中断优先级。数值越小，优先级越高。本课没有复杂中断竞争，设置为 1 即可。

`NVIC_EnableIRQ()` 让 CPU 接收 `TIM2_IRQn`。

TIM2 中断要真正进入 CPU，必须同时满足：

```text
TIM2 更新事件发生
  -> UIF = 1
  -> UIE = 1
  -> NVIC TIM2_IRQn enabled
```

### 7.10 第七步：启动计数器 `CEN`

代码是：

```c
TIM2->CR1 |= TIM_CR1_CEN;
```

`CEN` 是 Counter Enable。置 1 后，TIM2 的 `CNT` 开始按 PSC 后的频率递增。

这一步必须放在 PSC、ARR、中断配置之后。否则计数器可能在配置未完成时就开始运行，产生不干净的第一次更新事件。

### 7.11 `TIM2_IRQHandler()` 为什么先判断 `UIF`

TIM2 只有一个中断入口，但 TIM2 内部不只一种中断源。以后它还可能有捕获比较中断、触发中断等。

所以进入：

```c
void TIM2_IRQHandler(void)
```

后，先判断：

```c
if ((TIM2->SR & TIM_SR_UIF) != 0U) {
```

确认这次确实是更新事件。如果不判断，后续增加其他 TIM2 中断源时，业务逻辑会混乱。

### 7.12 为什么清 `UIF` 后再翻转 LED

中断里先执行：

```c
TIM2->SR &= ~TIM_SR_UIF;
```

再翻转 PC13。

先清标志的好处是尽快告诉硬件“这次更新事件已经处理”。如果中断业务稍微变长，先清标志可以降低重复进入的风险。

不清 `UIF` 的典型现象是：CPU 刚退出 ISR 又立刻进去，主循环几乎无法正常运行。

### 7.13 为什么主循环为空

本课主循环是：

```c
while (1) {
}
```

这正是定时器中断的意义：CPU 不需要在主循环里用空循环等 1 秒，也不需要不断轮询 `CNT`。定时器到了时间会通过中断通知 CPU。

后续工程中，主循环可以处理其他任务，而 TIM2 负责稳定地产生周期事件。

## 8. HAL 版代码逐步讲解

HAL 版在 [hal/src/main.c](hal/src/main.c)。

### 8.1 HAL 版整体流程

HAL 版主流程是：

```c
HAL_Init();
system_clock_72mhz_init();
led_pc13_init();
tim2_base_init();
HAL_TIM_Base_Start_IT(&htim2);
while (1) {
}
```

和寄存器版一样，主循环为空，LED 翻转由 TIM2 更新中断驱动。

区别是：寄存器版在 `tim2_base_init()` 里直接打开 `CEN`；HAL 版把“初始化”和“启动中断”分开，启动动作由 `HAL_TIM_Base_Start_IT()` 完成。

### 8.2 `TIM_HandleTypeDef htim2` 是什么

代码定义：

```c
static TIM_HandleTypeDef htim2;
```

`TIM_HandleTypeDef` 是 HAL 定时器句柄。它保存：

- `Instance`：具体是哪一个 TIM 外设
- `Init`：初始化参数
- HAL 内部状态、锁等管理信息

本课只有一个 TIM2，所以定义一个 `htim2`。如果以后同时用 TIM2、TIM3，就会有多个句柄。

### 8.3 `__HAL_RCC_TIM2_CLK_ENABLE()` 对应哪一步

HAL 版在 `tim2_base_init()` 里先执行：

```c
__HAL_RCC_TIM2_CLK_ENABLE();
```

它本质对应寄存器版：

```c
RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
```

这一步仍然必须有。HAL 不会凭空让没时钟的外设工作。

### 8.4 `htim2.Instance = TIM2` 是什么意思

代码是：

```c
htim2.Instance = TIM2;
```

它告诉 HAL：这个句柄后续操作的是 TIM2 这组寄存器。

如果 `Instance` 写成 TIM3，后面的 `Prescaler`、`Period` 等配置就会写到 TIM3，而不是 TIM2。现象可能是 TIM2 没反应，而另一个定时器被错误配置。

### 8.5 `Prescaler` 字段对应什么

代码是：

```c
htim2.Init.Prescaler = 7200U - 1U;
```

它对应寄存器版：

```c
TIM2->PSC = 7200U - 1U;
```

它表达的硬件意图是：把 72MHz TIM2 输入时钟除以 7200，得到 10kHz 计数频率。

HAL 字段名叫 `Prescaler`，但底层仍然写 TIM2 的 `PSC` 寄存器。

### 8.6 `Period` 字段对应什么

代码是：

```c
htim2.Init.Period = 10000U - 1U;
```

它对应寄存器版：

```c
TIM2->ARR = 10000U - 1U;
```

HAL 里叫 `Period`，底层含义就是自动重装载值。10kHz 计数频率下，10000 个计数就是 1 秒。

### 8.7 `CounterMode`、`ClockDivision`、`AutoReloadPreload` 是什么

本课设置：

```c
htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
```

含义是：

- `CounterMode = UP`：CNT 从 0 向上数到 ARR。
- `ClockDivision = DIV1`：不再对定时器内部采样时钟做额外分频。本课基础定时不用它改变周期。
- `AutoReloadPreload = DISABLE`：ARR 新值不使用预装载。本课初始化后不动态改 ARR，所以禁用即可。

这些字段不一定每节课都显眼，但它们仍然会影响 TIM 底层控制寄存器。

### 8.8 `HAL_TIM_Base_Init()` 底层做什么

代码是：

```c
HAL_TIM_Base_Init(&htim2)
```

它根据 `htim2.Instance` 找到 TIM2，根据 `htim2.Init` 写入基础定时相关寄存器，例如 `PSC`、`ARR`、计数模式等。

注意：本课代码在调用它之前已经手动 `__HAL_RCC_TIM2_CLK_ENABLE()`，所以 TIM2 时钟已开。

`HAL_TIM_Base_Init()` 只是初始化基础定时器，还没有打开更新中断，也没有启动计数器。

### 8.9 HAL 版 NVIC 配置在哪里

HAL 版在 `tim2_base_init()` 里执行：

```c
HAL_NVIC_SetPriority(TIM2_IRQn, 1, 0);
HAL_NVIC_EnableIRQ(TIM2_IRQn);
```

这和寄存器版 NVIC 配置对应。

HAL 的 `SetPriority` 多了子优先级参数，但本课重点仍然是：让 CPU 放行 TIM2 中断。

如果漏掉 `HAL_NVIC_EnableIRQ()`，`HAL_TIM_Base_Start_IT()` 即使打开了 TIM2 内部中断，CPU 也不会进入 `TIM2_IRQHandler()`。

### 8.10 `HAL_TIM_Base_Start_IT()` 做了什么

主函数里调用：

```c
HAL_TIM_Base_Start_IT(&htim2)
```

这个 API 是 HAL 版的启动关键。它会：

- 允许 TIM2 更新中断，对应 `DIER.UIE`
- 启动 TIM2 计数器，对应 `CR1.CEN`
- 更新 HAL 句柄状态

它和 `HAL_TIM_Base_Start()` 不同。普通 Start 只启动计数，不打开中断；本课必须用 `_IT` 版本。

### 8.11 `TIM2_IRQHandler()` 在 HAL 版里做什么

HAL 版中断入口是：

```c
void TIM2_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim2);
}
```

真正进入 CPU 的函数仍然叫 `TIM2_IRQHandler()`。只是这里不直接判断 `UIF`，而是交给 HAL 的 `HAL_TIM_IRQHandler()`。

HAL 会在里面检查 TIM2 的中断标志和使能位，清除相应标志，然后调用对应回调。

### 8.12 `HAL_TIM_PeriodElapsedCallback()` 为什么要判断 `htim->Instance`

回调是：

```c
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    }
}
```

这个函数名是 HAL 规定的弱回调。你在用户代码里重新定义它，HAL 定时器中断处理时就会调用。

参数 `htim` 告诉你是哪个定时器触发了回调。判断 `htim->Instance == TIM2` 是工程习惯：避免以后 TIM3、TIM4 也触发周期回调时误翻转 PC13。

## 9. 两个版本真正应该怎么学

### 9.1 为什么先学寄存器版

寄存器版能让你看到定时器中断的完整硬件链路：RCC 开时钟、PSC/ARR 定周期、UIF 表示事件、UIE 允许中断、NVIC 放行、ISR 清标志。

如果只看 HAL，很容易把 `HAL_TIM_Base_Start_IT()` 当成魔法，遇到“不进回调”时不知道该查 UIE、NVIC 还是标志位。

### 9.2 为什么再看 HAL 版

HAL 版更接近实际项目。它把 PSC、ARR、计数模式等配置放进 `TIM_HandleTypeDef`，把中断处理分发到统一回调。

这种写法适合多人维护，但前提是你知道每个字段对应寄存器版哪一步。

### 9.3 正确心智模型

本课要建立的映射是：

- `TIM2->PSC` -> `htim2.Init.Prescaler`
- `TIM2->ARR` -> `htim2.Init.Period`
- `TIM2->DIER.UIE + TIM2->CR1.CEN` -> `HAL_TIM_Base_Start_IT()`
- `TIM2_IRQHandler()` 手动处理 -> `HAL_TIM_IRQHandler()` 分发
- 手动翻转 LED -> `HAL_TIM_PeriodElapsedCallback()` 里翻转 LED

## 10. 检验问题清单

### 10.1 为什么 TIM2 输入时钟是 72MHz，而不是 PCLK1 的 36MHz？

答：本课 APB1 分频是 /2，不为 1。STM32F1 中当 APB 预分频不为 1 时，挂在该 APB 上的定时器时钟会变成 `2 x PCLK`，所以 TIM2CLK = 72MHz。

### 10.2 `PSC = 7200 - 1` 为什么得到 10kHz？

答：定时器分频系数是 `PSC + 1`。TIM2CLK 是 72MHz，除以 7200 后得到 10000Hz，也就是 10kHz。

### 10.3 `ARR = 10000 - 1` 为什么得到 1 秒更新？

答：PSC 后计数频率是 10kHz，每秒 10000 个 tick。计数器从 0 到 9999 一共 10000 个计数，所以 1 秒产生一次更新。

### 10.4 只产生 `UIF`，但不开 `UIE`，CPU 会进入中断吗？

答：不会。`UIF` 只是事件标志，`UIE` 才允许更新事件向 NVIC 发出中断请求。

### 10.5 开了 `UIE`，但没 `NVIC_EnableIRQ(TIM2_IRQn)`，会怎样？

答：TIM2 外设内部会发中断请求，但 CPU 的 NVIC 没放行，不会跳进 `TIM2_IRQHandler()`。

### 10.6 `TIM2_IRQHandler()` 里不清 `UIF` 会怎样？

答：中断标志一直保持置位，CPU 退出 ISR 后可能立刻再次进入，表现为程序被中断反复占住。

### 10.7 HAL 版的 `HAL_TIM_Base_Start()` 和 `HAL_TIM_Base_Start_IT()` 有什么区别？

答：普通 Start 只启动基础定时计数，不打开更新中断；Start_IT 会启动计数并打开更新中断。本课必须使用 Start_IT。

### 10.8 HAL 回调里为什么要判断 `htim->Instance == TIM2`？

答：多个定时器可能共用同一个周期回调。判断来源可以保证只有 TIM2 的更新事件才翻转 PC13。

## 11. 工程实现步骤

### 11.1 需求分析

本课需求是得到一个 1 秒周期事件：

- 不用主循环空转延时
- 由 TIM2 硬件稳定计数
- 到时间后通过中断通知 CPU
- 在中断或回调中翻转 PC13

需要的资源是：TIM2、NVIC、PC13 GPIO、72MHz 时钟基础。

### 11.2 硬件核查

本课无外部接线，但仍要确认：

- 板子 HSE 可用，系统时钟能到 72MHz。
- PC13 LED 正常，且低电平点亮。
- 程序下载成功，不是旧固件还在运行。

TIM2 是内部外设，不需要外部引脚；如果 LED 不闪，不要查 TIM2 引脚，因为本课没有用 TIM2 通道输出。

### 11.3 寄存器实现路线

按这个顺序：

1. 配系统时钟。确定 TIM2CLK 计算基础。
2. 初始化 PC13。准备观察现象。
3. 打开 TIM2 时钟。让 TIM2 外设开始工作。
4. 配 `PSC`。把 72MHz 分成 10kHz。
5. 配 `ARR`。让 10000 个 tick 形成 1 秒。
6. 清 `UIF`。避免旧更新标志误触发。
7. 开 `UIE`。允许更新事件发中断。
8. 配 NVIC。允许 CPU 接收 TIM2 中断。
9. 置 `CEN`。启动计数器。
10. 在 ISR 中判断并清 `UIF`，再翻转 PC13。

顺序错时，故障也不同。比如先开 `CEN` 再清标志，第一次中断可能不干净；只开 NVIC 不开 UIE，永远不进 ISR。

### 11.4 HAL 实现路线

按这个顺序：

1. `HAL_Init()`：准备 HAL 基础环境。
2. 配时钟和 PC13：和前面课程一致。
3. `__HAL_RCC_TIM2_CLK_ENABLE()`：打开 TIM2 时钟。
4. 填 `htim2.Instance = TIM2`：绑定外设。
5. 填 `Prescaler/Period/CounterMode`：表达 PSC/ARR/计数模式。
6. 调 `HAL_TIM_Base_Init()`：把配置写入 TIM2。
7. 配 `HAL_NVIC_SetPriority()` 和 `HAL_NVIC_EnableIRQ()`。
8. 调 `HAL_TIM_Base_Start_IT()`：启动计数并打开更新中断。
9. 在 `TIM2_IRQHandler()` 中调用 `HAL_TIM_IRQHandler()`。
10. 在 `HAL_TIM_PeriodElapsedCallback()` 中判断 TIM2 并翻转 LED。

HAL 版把标志检查和清除交给 `HAL_TIM_IRQHandler()`，但中断入口和 NVIC 仍然存在。

### 11.5 工程思维

学习阶段先写寄存器版，是为了明确“定时器到中断”并不是一个 API 完成的魔法，而是事件、标志、使能位、NVIC、ISR 共同组成的链路。

工程阶段使用 HAL，可以减少手写标志判断和清除的重复代码。尤其多个定时器共存时，HAL 回调能让业务逻辑集中。

长期维护时，要把定时器配置写成可计算、可解释的形式。看到 `PSC` 和 `ARR`，就应该能立刻算出周期，而不是只说“这个值能跑”。

### 11.6 常见工程陷阱

- 把 PCLK1 当 TIM2CLK：周期错一倍。
- 忘记开 `RCC_APB1ENR_TIM2EN`：TIM2 不计数。
- 忘记 `UIE`：`UIF` 可能置位，但不进中断。
- 忘记 NVIC：外设请求发出，但 CPU 不响应。
- ISR 不清 `UIF`：反复进入同一个中断。
- HAL 版用了 `HAL_TIM_Base_Start()`：计数启动了，但不会进周期回调。

## 12. 运行现象

正常情况下，PC13 每 1 秒翻转一次。因为翻转一次只是从亮到灭或从灭到亮，所以完整亮灭周期约 2 秒。

如果用调试器观察，`TIM2->CNT` 会持续递增，到 `ARR` 后产生更新并重新开始。`TIM2->SR` 的 `UIF` 会在更新时置位，并在中断处理中被清除。

## 13. 常见问题排查

### 13.1 LED 完全不闪

排查顺序：

1. 程序是否卡在系统时钟初始化。
2. PC13 是否初始化成输出。
3. TIM2 时钟是否打开。
4. `TIM2->CR1.CEN` 是否置位。
5. `TIM2_IRQHandler()` 是否正确命名。

如果 `TIM2->CNT` 在调试器里不动，重点查 TIM2 时钟和 CEN。

### 13.2 LED 闪烁周期不对

优先检查：

1. TIM2CLK 是否按 72MHz 计算。
2. `PSC` 是否是 `7200 - 1`。
3. `ARR` 是否是 `10000 - 1`。
4. APB1 分频是否为 /2。

如果你按 36MHz 算，却硬件按 72MHz 跑，周期会快一倍。

### 13.3 程序像卡在中断里

现象是 LED 可能快速异常变化，主循环或调试响应变差。

优先检查：

1. `TIM2_IRQHandler()` 是否清 `TIM_SR_UIF`。
2. 清标志方式是否正确。
3. 是否有其他 TIM2 标志也在触发中断。

最常见原因是不清 `UIF`。

### 13.4 HAL 版不进 `HAL_TIM_PeriodElapsedCallback()`

排查顺序：

1. 是否调用 `HAL_TIM_Base_Start_IT()`。
2. 是否配置 `HAL_NVIC_EnableIRQ(TIM2_IRQn)`。
3. `TIM2_IRQHandler()` 是否调用 `HAL_TIM_IRQHandler(&htim2)`。
4. `htim2.Instance` 是否等于 TIM2。

如果只调用 `HAL_TIM_Base_Init()`，定时器只是配置好了，并没有启动中断。

### 13.5 一上电立刻翻转一次

这通常和旧 `UIF` 标志有关。

检查启动前是否执行：

```c
TIM2->SR &= ~TIM_SR_UIF;
```

先清标志，再打开中断和启动计数，可以让第一次中断更干净。

## 14. 本课最核心的结论

1. TIM2 是 STM32 通用定时器外设，使用前必须先开 APB1 时钟。
2. APB1 分频不为 1 时，TIM2 输入时钟会变成 `2 x PCLK1`，本课就是 72MHz。
3. `PSC` 决定 CNT 计数频率，`ARR` 决定多久产生更新事件。
4. `UIF` 是事件标志，`UIE` 是事件中断使能，NVIC 是 CPU 接收中断的入口。
5. `CEN` 才是真正启动 TIM2 计数器的开关。
6. HAL 的 `Prescaler/Period/Start_IT/PeriodElapsedCallback` 都能对应回寄存器版链路。

## 15. 建议你现在怎么读这节课

建议顺序：

1. 先把第 5 章链路画一遍，尤其标出 `UIF`、`UIE`、NVIC 的区别。
2. 再手算 `72MHz / 7200 / 10000 = 1Hz`。
3. 然后读寄存器版 `tim2_base_init()`，逐句对应计算和中断链路。
4. 最后读 HAL 版，把 `Prescaler`、`Period`、`Start_IT` 翻译回 `PSC`、`ARR`、`UIE/CEN`。

能自己解释“为什么 CNT 在跑但不进中断”，这节课就算学到核心了。

## 16. 扩展练习

1. 把 `ARR` 改成 `5000 - 1`，观察翻转间隔变成 0.5 秒。
2. 把 `PSC` 改成 `72000 - 1`，再重新计算 ARR 应该怎么写。
3. 注释掉 `TIM_DIER_UIE`，观察是否还会进中断。
4. 注释掉清 `UIF`，观察故障现象。
5. 在调试器里观察 `TIM2->CNT`、`TIM2->SR`、`TIM2->DIER`、`TIM2->CR1`。

## 17. 下一课预告

下一课进入 [07_timer_output_compare](../07_timer_output_compare/README.md)。

本课只使用 TIM2 的“到时间产生更新中断”。下一课会使用定时器通道：当 `CNT` 计到 `CCR1` 时，不再只产生更新中断，而是让输出比较通道自动改变 PA0 的电平。那会把本课的 `CNT` 时间轴继续用起来。

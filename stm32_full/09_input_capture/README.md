# 第 9 课：输入捕获

## 1. 本课到底在学什么

前面你已经学过：

- 定时器可以自己计数
- 定时器可以定时产生中断
- 定时器可以输出 PWM

这节课我们切换到定时器的另一类非常重要的用途：

- **测量外部信号**

这就是：

- **输入捕获**

本课表面上的现象不是"再做一个闪灯"，而是：

- MCU 自己输出一个 1kHz 方波
- 再由另一个定时器把这个方波的周期测出来

也就是说，本课真正学习的是：

- 定时器怎样"记住某个边沿到来时的计数值"
- 为什么两个相邻边沿的计数差值就能代表周期
- 为什么 `CCR` 在输入捕获模式下，不再表示占空比，而表示"被捕获到的时间点"
- 怎样通过捕获值计算频率
- 寄存器版和 HAL 版之间如何一一对应

---

## 2. 本课学习目标

本课完成后，你应该能回答清楚这些问题：

- 输入捕获是什么
- `TIMx_CHy` 在输入捕获里扮演什么角色
- `CCR1` 在输入捕获模式下表示什么
- 什么叫"边沿时间戳"
- 为什么"当前捕获值 - 上一次捕获值"就能得到周期
- 为什么定时器预分频会直接影响测量精度
- `CCMR1 / CCER / DIER / SR` 在输入捕获里分别干什么
- 为什么 `uint16_t` 减法能正确处理计数器回绕
- `HAL_TIM_IC_Init()`、`HAL_TIM_IC_ConfigChannel()`、`HAL_TIM_IC_Start_IT()` 分别在做什么
- `HAL_TIM_ReadCapturedValue()` 和寄存器版读 `CCR1` 的关系

---

## 3. 本课目录结构

```text
09_input_capture/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

---

## 4. 实验硬件

### 4.1 开发板

- STM32F103C8T6 BluePill

### 4.2 下载器

- ST-Link

### 4.3 本课使用的两个引脚

| 引脚 | 功能 | 作用 |
|------|------|------|
| `PA0` | `TIM2_CH1` | 作为 **PWM 输出**，产生 1kHz 方波 |
| `PA6` | `TIM3_CH1` | 作为 **输入捕获输入**，接收被测信号 |

### 4.4 必须增加的一根杜邦线

请把：

- `PA0`

和：

- `PA6`

**直接用一根杜邦线连接起来。**

这条线的意义是：

- 让 MCU 自己输出的信号，再送回给自己测量

这样你不需要额外函数发生器，也能做完整输入捕获实验。

### 4.5 板载 LED

- `PC13`

本课中 LED 用来做一个最简单的结果指示：

- 如果测得频率在目标范围内，**LED 点亮**
- 否则 **LED 熄灭**

---

## 5. 先建立一个最基本的脑图

本课整条链路如下（这是一个你需要记在脑子里的画面）：

```
┌─────────────────────────────────────────────────────────┐
│                     STM32F103C8T6                       │
│                                                         │
│  ┌──────────┐      ┌──────────────┐                    │
│  │  TIM2    │      │   TIM3       │                    │
│  │  CH1     │──────│→ CH1         │                    │
│  │  PWM 输出│  ╱   │  输入捕获    │                    │
│  │  1kHz    │  ╲   │  1MHz 计数   │                    │
│  │  PA0     │ 导线 │  PA6         │                    │
│  └──────────┘      └──────┬───────┘                    │
│                           │                            │
│                   每当上升沿到来:                       │
│                   CCR1 = CNT（拍下时间戳）              │
│                   触发中断 → 计算差值                   │
└─────────────────────────────────────────────────────────┘
```

1. `TIM2_CH1` 在 `PA0` 输出 1kHz 方波
2. 用导线把 `PA0` 接到 `PA6`
3. `TIM3_CH1` 把 `PA6` 当作输入捕获信号
4. 每来一个上升沿，`TIM3` 就把 **当前计数器值** 复制到 `CCR1`
5. 程序在中断中读取 `CCR1`
6. 用这次捕获值减去上一次捕获值，得到两个上升沿之间的计数差
7. 这个差值就代表**一个周期经过了多少个计数单位**
8. 再根据定时器计数频率，计算出信号频率

**最关键的理解点是：**

- 输入捕获不是在"数边沿有多少个"
- 而是在"**记录边沿来的那一刻，定时器正数到哪里**"

---

## 6. 先认识本课里出现的核心名词

### 6.1 什么是输入捕获

输入捕获可以先理解成：

- 当外部信号的某个边沿到来时
- 把当前定时器计数器的值"**抓拍下来**"

这个"抓拍下来的值"就叫：

- **捕获值**

这和"拍照"很像：

- 你拿着相机（定时器）
- 看到某个事件（边沿）发生时
- 按下快门（硬件自动触发）
- 拍下当前的时间（计数值）

### 6.2 `CCR1` 在输入捕获里表示什么

你在 PWM 里已经见过：

- `CCR1`

但在 PWM 模式下，它表示的是：

- **比较值**
- 高电平持续长度的边界

而在输入捕获模式下，`CCR1` 不再表示占空比，而表示：

- **最近一次捕获到的计数值**

也就是说，同一个寄存器：

- **在不同模式下，意义完全不同**

这点一定要建立起来。

### 6.3 什么叫"边沿时间戳"

如果定时器当前正在计数：

- 0, 1, 2, 3, 4, 5, ...

假设某个上升沿到来时，当前计数值正好是：

- 1234

那么：

- `CCR1 = 1234`

这就相当于给这个边沿打了一个"**时间戳**"。

### 6.4 为什么两个捕获值相减就能得到周期

假设：

- 第一次上升沿时，捕获值是 100
- 第二次上升沿时，捕获值是 1100

那么两次上升沿之间经过的计数数目就是：

```
1100 - 100 = 1000
```

如果定时器计数频率是：

- 1MHz

那么每个计数单位就是：

- 1us

所以：

- 1000 个计数 = 1000us = 1ms

那么频率就是：

- 1 / 1ms = 1kHz

### 6.5 为什么本课把 TIM3 的计数频率设成 1MHz

因为这样特别直观：

- 1 个计数 = 1us

于是：

- 捕获差值是多少
- 就几乎直接对应多少微秒

这会让你后面理解周期、脉宽、频率都轻松很多。

### 6.6 关于计数器回绕：为什么 uint16_t 减法仍然正确

`TIM3->ARR = 0xFFFF`，所以 CNT 最大到 65535 就会回绕到 0。

假设：

- 上次捕获值 = 65500
- 本次捕获值 = 200

直观上 `200 - 65500 = -65300`，但因为是 `uint16_t` 减法，实际上发生的是：

```
  200
- 65500
--------
  结果 = (200 + 65536 - 65500) = 236   ← 这就是正确的周期
```

**这是 C 语言中无符号数减法的特性：**

- 结果在数学上等价于 `(本次值 + 65536 - 上次值) % 65536`
- 当计数器回绕时，这个计算结果恰好就是正确经过的计数个数

所以只要：

1. 使用 `uint16_t` 类型
2. 保证两次捕获之间 CNT 只回绕一次（周期 < ARR 值）

这种减法就是正确的，你**不需要额外处理回绕**。

### 6.7 `CCMR1` 在输入捕获里干什么

`CCMR1` 仍然是通道模式寄存器，但在输入捕获模式下，它负责的东西变成了：

- 通道作为输入还是输出
- 通道到底接 TI1 还是 TI2
- 输入滤波和预分频

#### CCMR1 的位布局（本课使用的部分）

```
bit 0-1: CC1S    — 通道 1 方向/映射选择
bit 2-3: IC1PSC  — 输入预分频器
bit 4-7: IC1F    — 输入滤波器
```

**CC1S（Capture/Compare Selection）详解：**

| CC1S 值 | 含义 |
|---------|------|
| 00 | 通道 1 作为 **输出**（PWM/比较） |
| **01** | 通道 1 作为 **输入**，映射到 **TI1**（本课使用） |
| 10 | 通道 1 作为 **输入**，映射到 **TI2** |
| 11 | 通道 1 作为 **输入**，映射到 **TRC**（内部触发） |

**IC1PSC（Input Capture Prescaler）详解：**

| 值 | 分频比 | 含义 |
|----|--------|------|
| 00 | 1 | 每个边沿都捕获（本课使用） |
| 01 | 2 | 每 2 个边沿捕获一次 |
| 10 | 4 | 每 4 个边沿捕获一次 |
| 11 | 8 | 每 8 个边沿捕获一次 |

**IC1F（Input Filter）详解：**

滤波的原理是：在检测到边沿后，再连续采样 N 次确认电平，如果 N 次都一致，才认为是一个有效边沿，可滤除短于 N 个采样周期的毛刺。值越大滤波越强，但延迟也越大。本课信号来自内部连线，不使用滤波。

### 6.8 `CCER` 在输入捕获里干什么

`CCER` 用来决定：

- 通道是否使能
- 采集上升沿还是下降沿

#### CCER 中与本课相关的位

```
bit 0: CC1E  — 通道 1 捕获使能
bit 1: CC1P  — 通道 1 极性选择
bit 3: CC1NP — 通道 1 极性选择（互补位）
```

**边沿选择（CC1P + CC1NP）：**

| CC1P | CC1NP | 捕获边沿 |
|------|-------|---------|
| **0** | **0** | **上升沿（本课使用）** |
| 1 | 0 | 下降沿 |
| 0 | 1 | 保留 |
| 1 | 1 | 双沿 |

### 6.9 `DIER` 和 `SR` 在输入捕获里分别干什么

和基础定时器类似：

- `DIER` 负责中断使能
- `SR` 负责状态标志

**DIER 与本课相关的位：**

```
bit 0: UIE   — 更新中断使能
bit 1: CC1IE — 通道 1 捕获中断使能（本课使用）
```

**SR 与本课相关的位：**

```
bit 0: UIF   — 更新中断标志
bit 1: CC1IF — 通道 1 捕获完成标志（本课使用）
```

当一个捕获事件发生时：

1. 硬件将 `SR.CC1IF` 置 1
2. 如果 `DIER.CC1IE` 已使能，触发中断请求
3. CPU 响应中断，执行 `TIM3_IRQHandler()`
4. 软件读取 `CCR1` 后，手动清除 `CC1IF`

### 6.10 为什么本课还要保留一个输出定时器 TIM2

因为输入捕获需要一个"被测信号"。

为了让这节课能独立完成，不依赖额外设备，本课直接让 MCU 自己：

- 一边输出
- 一边测量

这也是为什么本课会同时出现：

- `TIM2`：信号源
- `TIM3`：测量器

---

## 7. 本课 Demo 要实现什么

本课 Demo 设计如下：

1. `TIM2_CH1` 在 `PA0` 输出 1kHz 方波
2. 用杜邦线把 `PA0` 接到 `PA6`
3. `TIM3_CH1` 对 `PA6` 的上升沿做输入捕获
4. 计算两次捕获的差值
5. 若测得周期约为 1000 个计数，则判定频率正确
6. 频率正确时点亮 `PC13`，否则熄灭

### 7.1 为什么目标差值是 1000

因为本课让：

- `TIM3` 计数频率 = 1MHz

而被测信号频率是：

- 1kHz

所以周期是：

- 1ms

而：

- 1ms = 1000us

所以两个相邻上升沿之间应该差：

- 1000 个计数

---

## 8. 寄存器版代码逐步讲解

对应代码文件：

- [reg/src/main.c](/home/myself/workspace/mcu/stm32/STM32F103C8T6/09_input_capture/reg/src/main.c)

### 8.1 整体结构

```c
// —— 全局变量 ——
static volatile uint16_t g_last_capture;    // 上次捕获值
static volatile uint16_t g_period_ticks;    // 当前周期（计数差）
static volatile uint8_t  g_capture_ready;   // 是否有新数据

// —— 各初始化函数 ——
system_clock_72mhz_init();   // 时钟到 72MHz
led_pc13_init();             // LED 引脚
tim2_pwm_output_init();      // TIM2 CH1 = 1kHz 输出
pa6_input_pin_init();        // PA6 为输入浮空
tim3_input_capture_init();   // TIM3 CH1 = 输入捕获

// —— 中断处理 ——
TIM3_IRQHandler();           // 读取 CCR1，计算周期

// —— 主循环 ——
// 如果捕获数据就绪，判断是否接近 1000
```

### 8.2 全局变量解释

```c
static volatile uint16_t g_last_capture = 0U;
static volatile uint16_t g_period_ticks = 0U;
static volatile uint8_t g_capture_ready = 0U;
```

| 变量 | 类型 | 作用 |
|------|------|------|
| `g_last_capture` | `uint16_t` | 保存上一次捕获到的 `CCR1` 值 |
| `g_period_ticks` | `uint16_t` | 本次捕获值减去上次捕获值，代表周期 |
| `g_capture_ready` | `uint8_t` | 中断中置 1，主循环检测到后处理 |

**为什么是 `uint16_t`？**  
因为 `TIM3->CCR1` 是 16 位寄存器（ARR = 0xFFFF），所以捕获值最大 65535，`uint16_t` 刚好匹配。

**为什么加 `volatile`？**  
因为 `g_last_capture` 和 `g_period_ticks` 在中断中修改，主循环中读取，`volatile` 告诉编译器不要优化掉这些读写操作。

**为什么 `g_capture_ready` 也是 `volatile`？**  
因为它在中断中被置 1，在主循环中被检测，跨中断/主循环的共享变量必须加 `volatile`。

### 8.3 `system_clock_72mhz_init()` 简述

```c
static void system_clock_72mhz_init(void)
{
    // 1. 打开 HSE 外部晶振
    // 2. 等待 HSE 就绪
    // 3. 配置 AHB = 不分频, APB1 = 2 分频, APB2 = 不分频
    // 4. 配置 PLL 时钟源 = HSE, PLL 倍频 = 9 倍
    // 5. 打开 PLL, 等待就绪
    // 6. 切换系统时钟到 PLL
    // 7. 等待切换完成
}
```

关键点：72MHz = 8MHz（HSE）× 9（PLL 倍频）

### 8.4 `led_pc13_init()` 简述

```c
static void led_pc13_init(void)
{
    // 1. 打开 GPIOC 时钟
    // 2. PC13 配置为推挽输出（50MHz）
    // 3. 初始输出高电平（LED 灭）
}
```

### 8.5 `tim2_pwm_output_init()`

```c
static void tim2_pwm_output_init(void)
{
    /*
     * PA0 = TIM2_CH1 输出
     */
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN;

    GPIOA->CRL &= ~(GPIO_CRL_MODE0 | GPIO_CRL_CNF0);
    GPIOA->CRL |= GPIO_CRL_MODE0_1 | GPIO_CRL_CNF0_1;

    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;

    /*
     * 72MHz / 72 = 1MHz
     * 1MHz / 1000 = 1kHz
     */
    TIM2->PSC = 72U - 1U;
    TIM2->ARR = 1000U - 1U;
    TIM2->CCR1 = 500U;

    TIM2->CCMR1 &= ~(TIM_CCMR1_CC1S | TIM_CCMR1_OC1M);
    TIM2->CCMR1 |= TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2;
    TIM2->CCMR1 |= TIM_CCMR1_OC1PE;
    TIM2->CCER |= TIM_CCER_CC1E;
    TIM2->EGR |= TIM_EGR_UG;
    TIM2->CR1 |= TIM_CR1_CEN;
}
```

**PA0 引脚配置：**

`GPIOA->CRL |= GPIO_CRL_MODE0_1 | GPIO_CRL_CNF0_1`：

- `MODE0_1`（bit 9）：输出模式，50MHz
- `CNF0_1`（bit 3）：复用推挽输出（AF Push-Pull）

因为 PA0 的第二功能就是 `TIM2_CH1`，所以需要配成复用功能。

**PWM 参数：**

- `PSC = 71`：72MHz / 72 = 1MHz
- `ARR = 999`：1MHz / 1000 = 1kHz
- `CCR1 = 500`：50% 占空比

**通道模式：**

`OC1M = 110`（`TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2`）：PWM 模式 1

- 计数值 < CCR1 时输出有效电平
- 计数值 ≥ CCR1 时输出无效电平

`OC1PE = 1`：预装载使能，更新事件时才真正生效。

**最后两步：**

```c
TIM2->EGR |= TIM_EGR_UG;   // 产生更新事件，立即加载 PSC/ARR
TIM2->CR1 |= TIM_CR1_CEN;  // 启动 TIM2
```

### 8.6 `pa6_input_pin_init()`

```c
static void pa6_input_pin_init(void)
{
    /*
     * PA6 = TIM3_CH1 输入
     *
     * 作为输入捕获输入，这里先配置为浮空输入即可。
     */
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;

    GPIOA->CRL &= ~(GPIO_CRL_MODE6 | GPIO_CRL_CNF6);
    GPIOA->CRL |= GPIO_CRL_CNF6_0;
}
```

`CNF6_0 = 1, MODE6 = 00`：

- `MODE` 两位全 0 → 输入模式
- `CNF` = 01（`CNF6_0 = 1, CNF6_1 = 0`）→ **浮空输入**

为什么是浮空输入？

- 因为信号由 TIM2 从 PA0 直接通过杜邦线送到 PA6
- 不需要上拉或下拉，浮空输入即可

### 8.7 `tim3_input_capture_init()`

这是本课的核心函数，逐行讲解。

```c
static void tim3_input_capture_init(void)
{
    /*
     * TIM3 挂在 APB1 上。
     */
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
```

**第 1 步：开 TIM3 时钟**

TIM3 在 APB1 总线上，用 `RCC->APB1ENR` 的 `TIM3EN` 位（bit 1）使能。

```c
    /*
     * 让 TIM3 计数频率为 1MHz：
     * 72MHz / 72 = 1MHz
     *
     * 这样 1 个计数 = 1us。
     */
    TIM3->PSC = 72U - 1U;
    TIM3->ARR = 0xFFFFU;
```

**第 2 步：PSC 和 ARR**

`PSC = 71` 是因为：

- TIM3 输入时钟 = 72MHz
- 72MHz / (71 + 1) = 1MHz
- 所以 1 个计数 = 1us

`ARR = 0xFFFF`（65535）是因为：

- 我们希望计数器尽可能宽，以测量最长可能的信号
- 最大测量周期 ≈ 65535us ≈ 65.5ms
- 对应最低可测频率 ≈ 15Hz
- 1kHz 信号周期只有 1000us，远小于最大值

```c
    /*
     * 通道 1 配成输入，并映射到 TI1。
     *
     * CC1S = 01
     */
    TIM3->CCMR1 &= ~(TIM_CCMR1_CC1S |
                     TIM_CCMR1_IC1PSC |
                     TIM_CCMR1_IC1F);
    TIM3->CCMR1 |= TIM_CCMR1_CC1S_0;
```

**第 3 步：CCMR1 配置**

`TIM3->CCMR1 &= ~(...)`：先清除这三个字段（CC1S、IC1PSC、IC1F）的所有位。

`TIM3->CCMR1 |= TIM_CCMR1_CC1S_0`：把 `CC1S` 设为 01。

| CC1S | 含义 |
|------|------|
| **01** | 通道 1 作为 **输入**，映射到 **TI1**（即 PA6 引脚信号） |

一旦设成 01，`CCR1` 就不再是比较值，而是输入捕获寄存器。

IC1PSC = 00：不分频，每个边沿都捕获。
IC1F = 0000：不滤波。

```c
    /*
     * 选择上升沿捕获。
     * CC1P = 0 表示上升沿。
     */
    TIM3->CCER &= ~(TIM_CCER_CC1P | TIM_CCER_CC1NP);
    TIM3->CCER |= TIM_CCER_CC1E;
```

**第 4 步：CCER 配置**

`TIM3->CCER &= ~(CC1P | CC1NP)`：清除极性位 → CC1P=0, CC1NP=0 → **上升沿捕获**。

`TIM3->CCER |= CC1E`：使能通道 1 捕获。CC1E=1 是捕获功能的"总开关"。

如果不使能 `CC1E`，边沿到来了也不会触发捕获，CCR1 不会被更新，CC1IF 也不会被置位。

```c
    /*
     * 允许通道 1 捕获中断。
     */
    TIM3->DIER |= TIM_DIER_CC1IE;
```

**第 5 步：DIER 中断使能**

`CC1IE`（Capture/Compare 1 Interrupt Enable）：

- 0：禁止捕获中断（只能轮询 `SR.CC1IF`）
- **1**：允许捕获中断

本课使用中断方式，在主循环中等待 `g_capture_ready` 被置位，而不是轮询 `SR`。

```c
    /*
     * 先清通道 1 捕获标志。
     */
    TIM3->SR &= ~TIM_SR_CC1IF;
```

**第 6 步：清除 SR 标志**

用 `&= ~TIM_SR_CC1IF` 把 `CC1IF` 位清零。

为什么要在启动前先清一次？  
- 上电后这个标志可能处于不确定的状态
- 如果不清，可能一进入中断就被误触发
- 这是"先打扫干净再接待客人"的习惯操作

```c
    NVIC_SetPriority(TIM3_IRQn, 1U);
    NVIC_EnableIRQ(TIM3_IRQn);
```

**第 7 步：NVIC 配置**

`NVIC_SetPriority(TIM3_IRQn, 1U)`：

- 设置 TIM3 中断的抢占优先级为 1
- STM32F103 使用 4 位优先级，数值越小优先级越高
- 优先级 1 比默认的 0 低，但这里不重要

`NVIC_EnableIRQ(TIM3_IRQn)`：

- TIM3 内部中断已使能（CC1IE=1）
- 但还需要打开 NVIC 这道总闸
- 不调这个函数，中断信号无法到达 CPU 内核

```c
    TIM3->CR1 |= TIM_CR1_CEN;
}
```

**第 8 步：启动计数器**

`CEN = 1`，TIM3 开始递增计数。

**之前的所有配置都是铺路，这一行才是真正开车。** 如果没有这一行，计数器停在 0，永远不会发生捕获事件。

### 8.8 `TIM3_IRQHandler()` —— 中断处理函数

```c
void TIM3_IRQHandler(void)
{
    if ((TIM3->SR & TIM_SR_CC1IF) != 0U) {
        uint16_t current_capture = (uint16_t)TIM3->CCR1;

        g_period_ticks = (uint16_t)(current_capture - g_last_capture);
        g_last_capture = current_capture;
        g_capture_ready = 1U;

        TIM3->SR &= ~TIM_SR_CC1IF;
    }
}
```

**为什么要判断 `CC1IF`？**

一个中断源可能有多个中断标志（例如更新中断、多个通道捕获中断）。所以进入中断后，先检查 `CC1IF` 是否确实被置位，确认本次中断是通道 1 捕获引起的。

**读取 CCR1：**

```c
uint16_t current_capture = (uint16_t)TIM3->CCR1;
```

- `TIM3->CCR1` 在输入捕获模式下是"只读"的（实际上读操作不改变寄存器值）
- 它保存了硬件在上升沿到来那一刻自动锁存的计数值

**计算周期：**

```c
g_period_ticks = (uint16_t)(current_capture - g_last_capture);
```

- 第一次进入时，`g_last_capture = 0`，`current_capture` 是第一次上升沿的计数值
- 第二次进入时，`g_last_capture` 是第一次的值，`current_capture` 是第二次的值
- 差值就是两个上升沿之间经过了多少计数
- `uint16_t` 减法自动处理回绕，见 6.6 节

**状态更新：**

```c
g_capture_ready = 1U;
```

- 通知主循环："有新数据了，可以处理了"

**清除标志：**

```c
TIM3->SR &= ~TIM_SR_CC1IF;
```

- 必须清除，否则退出中断后同样的中断会立即再次触发
- 不清除 = 永远卡在同一个中断里

### 8.9 `main()` 主循环

```c
int main(void)
{
    system_clock_72mhz_init();
    led_pc13_init();
    tim2_pwm_output_init();
    pa6_input_pin_init();
    tim3_input_capture_init();

    while (1) {
        if (g_capture_ready != 0U) {
            if ((g_period_ticks >= 990U) && (g_period_ticks <= 1010U)) {
                GPIOC->BRR = GPIO_BRR_BR13;   // LED 亮（低电平）
            } else {
                GPIOC->BSRR = GPIO_BSRR_BS13;  // LED 灭（高电平）
            }
        }
    }
}
```

**为什么判断 990 ~ 1010？**

- 理论值是 1000
- 但实际信号可能存在微小抖动
- ±1% 的容差范围是合理的

**为什么用 `BRR` 和 `BSRR` 控制 LED？**

- `BRR`（Bit Reset Register）：写 1 → 对应引脚输出低电平
- `BSRR`（Bit Set Reset Register）：低 16 位写 1 → 对应引脚输出高电平

PC13 的 LED 是**低电平点亮**，所以：

- `BRR` → 低电平 → LED 亮
- `BSRR` → 高电平 → LED 灭

---

## 9. HAL 版代码逐步讲解

对应代码文件：

- [hal/src/main.c](/home/myself/workspace/mcu/stm32/STM32F103C8T6/09_input_capture/hal/src/main.c)

### 9.1 HAL 操作模型

HAL 将外设抽象为**句柄结构体**，所有操作围绕句柄展开：

```c
TIM_HandleTypeDef htim3;   // TIM3 的句柄
```

初始化流程：

1. 初始化句柄的 `Init` 成员
2. 调用 `HAL_TIM_IC_Init()` 应用基础配置
3. 初始化 `TIM_IC_InitTypeDef` 配置通道参数
4. 调用 `HAL_TIM_IC_ConfigChannel()` 应用通道配置
5. 调用 `HAL_TIM_IC_Start_IT()` 启动捕获并开启中断

### 9.2 全局变量

```c
static volatile uint32_t g_last_capture = 0U;
static volatile uint32_t g_period_ticks = 0U;
static volatile uint8_t g_capture_ready = 0U;
```

注意 HAL 版中使用的是 `uint32_t`，因为 `HAL_TIM_ReadCapturedValue()` 返回 `uint32_t`。

### 9.3 `tim3_input_capture_init()` —— HAL 版

```c
static void tim3_input_capture_init(void)
{
    TIM_IC_InitTypeDef sConfigIC = {0};   // 通道配置结构体

    __HAL_RCC_TIM3_CLK_ENABLE();           // 开 TIM3 时钟
```

**`TIM_IC_InitTypeDef`** 是 HAL 为输入捕获提供的专用结构体，包含捕获极性、映射、预分频、滤波参数。

```c
    htim3.Instance = TIM3;
    htim3.Init.Prescaler = 72U - 1U;
    htim3.Init.Period = 0xFFFFU;
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
```

**寄存器版对应：**

| HAL 字段 | 对应寄存器 | 本课值 |
|----------|-----------|--------|
| `Prescaler` | `TIMx->PSC` | 71 |
| `Period` | `TIMx->ARR` | 65535 |
| `CounterMode` | `TIMx->CR1.DIR` | 向上计数 |
| `ClockDivision` | `TIMx->CR1.CKD` | 不分频 |
| `AutoReloadPreload` | `TIMx->CR1.ARPE` | 禁止预装载 |

```c
    if (HAL_TIM_IC_Init(&htim3) != HAL_OK) {
        error_handler();
    }
```

**`HAL_TIM_IC_Init()` 在做什么？**

这个函数内部做了一系列寄存器操作：

1. 检查传入句柄参数合法性
2. 如果句柄关联了 HAL 的 `MspInit` 回调，会调用它（通常用于 GPIO / RCC / NVIC）
3. 根据 `Init` 成员的值，写入 `TIMx->CR1`、`TIMx->PSC`、`TIMx->ARR` 等寄存器
4. 配置时基（PSC + ARR）
5. 产生 UG 更新事件加载新值

```c
    sConfigIC.ICPolarity = TIM_INPUTCHANNELPOLARITY_RISING;
    sConfigIC.ICSelection = TIM_ICSELECTION_DIRECTTI;
    sConfigIC.ICPrescaler = TIM_ICPSC_DIV1;
    sConfigIC.ICFilter = 0U;
```

**与寄存器版的一一对应：**

| HAL 字段 | 对应寄存器位 | 本课值 | 含义 |
|----------|------------|--------|------|
| `ICPolarity` | `CCER.CC1P` + `CCER.CC1NP` | RISING | 上升沿捕获 |
| `ICSelection` | `CCMR1.CC1S` | DIRECTTI (= 01) | 映射到 TI1 |
| `ICPrescaler` | `CCMR1.IC1PSC` | DIV1 | 不分频 |
| `ICFilter` | `CCMR1.IC1F` | 0 | 不滤波 |

```c
    if (HAL_TIM_IC_ConfigChannel(&htim3, &sConfigIC, TIM_CHANNEL_1) != HAL_OK) {
        error_handler();
    }
```

**`HAL_TIM_IC_ConfigChannel()` 在做什么？**

这个函数把 `sConfigIC` 的值写入 `CCMR1` 和 `CCER` 的相关位：

1. 根据 `ICSelection` 设置 `CCMR1.CC1S`
2. 根据 `ICPrescaler` 设置 `CCMR1.IC1PSC`
3. 根据 `ICFilter` 设置 `CCMR1.IC1F`
4. 根据 `ICPolarity` 设置 `CCER.CC1P` 和 `CCER.CC1NP`
5. 设置 `CCER.CC1E = 1`（使能捕获）

```c
    HAL_NVIC_SetPriority(TIM3_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(TIM3_IRQn);
}
```

**NVIC 配置与寄存器版完全一致**，只不过 HAL 对 `NVIC_SetPriority` 提供了一个不同的接口（主优先级 + 子优先级）。

### 9.4 启动捕获

```c
if (HAL_TIM_IC_Start_IT(&htim3, TIM_CHANNEL_1) != HAL_OK) {
    error_handler();
}
```

**`HAL_TIM_IC_Start_IT()` 在做什么？**

1. 使能 `DIER.CC1IE`（允许捕获中断）
2. 清除 `SR.CC1IF`（清标志）
3. 设置 `CR1.CEN = 1`（启动计数器）

**对应寄存器版：**

| 寄存器版操作 | HAL 对应 |
|-------------|----------|
| `TIM3->DIER |= CC1IE` | 在 `Start_IT` 内完成 |
| `TIM3->SR &= ~CC1IF` | 在 `Start_IT` 内完成 |
| `TIM3->CR1 |= CEN` | 在 `Start_IT` 内完成 |

### 9.5 中断处理链

```c
void TIM3_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim3);
}
```

**HAL 的中断处理方式：**

与寄存器版不同，HAL 要求用户在 `TIM3_IRQHandler()` 中调用通用的 `HAL_TIM_IRQHandler()`。

`HAL_TIM_IRQHandler()` 内部会：

1. 检测 `SR.CC1IF` 是否置位
2. 如果置位，且 `DIER.CC1IE` 已使能
3. 调用对应的回调函数 `HAL_TIM_IC_CaptureCallback()`
4. 清除 `SR.CC1IF`

### 9.6 捕获回调函数

```c
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM3 && htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1) {
        uint32_t current_capture = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);

        g_period_ticks = (uint16_t)(current_capture - g_last_capture);
        g_last_capture = current_capture;
        g_capture_ready = 1U;
    }
}
```

**为什么判断 `htim->Instance == TIM3`？**
如果多个定时器共用同一个回调函数，这个判断能区分是哪个定时器触发的。

**为什么判断 `htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1`？**
确认是通道 1 的捕获事件。

**`HAL_TIM_ReadCapturedValue()` 对应寄存器版的什么？**

```c
// HAL 内部实现简化：
uint32_t HAL_TIM_ReadCapturedValue(TIM_HandleTypeDef *htim, uint32_t Channel)
{
    return htim->Instance->CCR1;  // 就是读 CCR1 寄存器
}
```

所以 `HAL_TIM_ReadCapturedValue(&htim3, TIM_CHANNEL_1)` 等价于 `TIM3->CCR1`。

### 9.7 PWM 初始化（HAL 版）

```c
static void tim2_pwm_init(void)
{
    TIM_OC_InitTypeDef sConfigOC = {0};   // 输出比较配置结构体

    __HAL_RCC_TIM2_CLK_ENABLE();

    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 72U - 1U;
    htim2.Init.Period = 1000U - 1U;
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_PWM_Init(&htim2) != HAL_OK) {
        error_handler();
    }

    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 500U;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;

    if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1) != HAL_OK) {
        error_handler();
    }
}
```

| HAL 字段 | 对应寄存器 | 值 |
|----------|-----------|-----|
| `Prescaler` | `TIM2->PSC` | 71 |
| `Period` | `TIM2->ARR` | 999 |
| `OCMode` | `CCMR1.OC1M` | PWM1 |
| `Pulse` | `TIM2->CCR1` | 500 |

启动时使用：

```c
HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
```

### 9.8 `error_handler()`

```c
static void error_handler(void)
{
    __disable_irq();   // 关闭全局中断
    while (1) {        // 死循环，等待调试器介入
    }
}
```

当 HAL 函数返回 HAL_OK 以外的值时，说明配置失败，程序卡在这里。你可以用调试器挂上来查看问题。

---

## 10. 寄存器版和 HAL 版对比

### 10.1 寄存器版重点看什么

- `CCR1` 在输入捕获模式下的意义变化
- 为什么两次捕获值之差就是周期
- `CCMR1 / CCER / DIER / SR` 在输入捕获链路中的作用
- 为什么 `uint16_t` 减法能正确处理计数器回绕
- 先清标志再启动的习惯操作

### 10.2 HAL 版重点看什么

- HAL 如何表达"这个定时器通道是输入捕获"
- HAL 如何通过回调组织测量逻辑
- `TIM_IC_InitTypeDef` 结构体的各个成员对应哪些寄存器

### 10.3 两者如何一一对应

| 功能 | 寄存器版 | HAL 版 |
|------|---------|--------|
| 开时钟 | `RCC->APB1ENR |= TIM3EN` | `__HAL_RCC_TIM3_CLK_ENABLE()` |
| 设 PSC | `TIM3->PSC = 71` | `htim3.Init.Prescaler = 71` |
| 设 ARR | `TIM3->ARR = 0xFFFF` | `htim3.Init.Period = 0xFFFF` |
| 设输入捕获 | `CCMR1.CC1S = 01` | `sConfigIC.ICSelection = DIRECTTI` |
| 设边沿 | `CCER.CC1P = 0` | `sConfigIC.ICPolarity = RISING` |
| 使能捕获 | `CCER.CC1E = 1` | （合并到 ConfigChannel） |
| 开中断 | `DIER.CC1IE = 1` | `HAL_TIM_IC_Start_IT()` |
| 清标志 | `SR &= ~CC1IF` | （HAL 自动清） |
| 读捕获值 | `TIM3->CCR1` | `HAL_TIM_ReadCapturedValue()` |
| 中断函数 | 自己写完整逻辑 | 调 `HAL_TIM_IRQHandler()` |
| 捕获逻辑 | 写在中断里 | 写在 `CaptureCallback` 回调 |

---

## 11. 运行现象

如果程序正常，并且你已经把：

- `PA0`
- `PA6`

用杜邦线连起来，那么你应该看到：

- `PC13` **亮起**，表示测得频率在目标范围附近

如果你把连接线拔掉，或者接错，LED 通常会熄灭。

---

## 12. 常见问题排查

### 12.1 LED 一直不亮

优先检查：

1. **是否真的把 `PA0` 接到了 `PA6`** —— 这是最常见的疏忽
2. **`TIM2` 是否真的输出了波形** —— 可以用示波器或逻辑分析仪看 PA0
3. **`TIM3` 是否真的启动了输入捕获** —— 检查 `CR1.CEN` 是否为 1
4. **`CC1IF` 是否有变化** —— 在中断中设断点观察
5. **是否真的在上升沿捕获** —— 检查 `CCER.CC1P` 是否为 0
6. **中断是否真的进了** —— 在 `TIM3_IRQHandler` 设断点

### 12.2 捕获结果很不稳定

优先检查：

1. 连线是否接触良好 —— 杜邦线插紧了吗
2. 输入引脚模式是否正确 —— `CNF6` 应为浮空输入
3. 是否配置了不合适的预分频或滤波
4. 信号质量是否良好 —— 如果 PA0 到 PA6 的线太长，可能引入噪声

### 12.3 HAL 回调不进

优先检查：

1. 是否调用了 `HAL_TIM_IC_Start_IT()` —— 不仅是 Init
2. `TIM3_IRQHandler()` 里是否调用了 `HAL_TIM_IRQHandler(&htim3)` —— 这是入口
3. `NVIC` 是否打开了对应 IRQ —— `HAL_NVIC_EnableIRQ(TIM3_IRQn)`
4. 所有 HAL 函数是否返回 `HAL_OK` —— 用 `error_handler` 捕获

---

## 13. 本课要点总结

本课最核心的结论如下：

1. **输入捕获的核心是"记录边沿到来时的计数值"**
2. **`CCR` 在不同模式下意义不同** —— 输出是比较值，输入是时间戳
3. **两次相邻捕获值的差就是周期对应的计数差**
4. **频率计算离不开定时器计数频率**
5. **`uint16_t` 无符号减法自动处理计数器回绕**，无需额外处理
6. **输入捕获是测量频率、周期、脉宽的重要基础**
7. **HAL 版与寄存器版的功能一一对应**，理解寄存器版有助于理解 HAL 内部机制

### 13.1 寄存器操作速查表

| 寄存器 | 位/字段 | 操作 | 含义 |
|--------|---------|------|------|
| `RCC->APB1ENR` | TIM3EN | 写 1 | 开启 TIM3 时钟 |
| `TIM3->PSC` | [15:0] | 71 | 计数频率 = 72MHz / 72 = 1MHz |
| `TIM3->ARR` | [15:0] | 0xFFFF | 计数器范围 0~65535 |
| `TIM3->CCMR1` | CC1S[1:0] | 01 | 通道 1 为输入，映射 TI1 |
| `TIM3->CCMR1` | IC1PSC[1:0] | 00 | 输入不分频 |
| `TIM3->CCMR1` | IC1F[3:0] | 0000 | 不滤波 |
| `TIM3->CCER` | CC1P + CC1NP | 00 | 上升沿捕获 |
| `TIM3->CCER` | CC1E | 1 | 使能捕获 |
| `TIM3->DIER` | CC1IE | 1 | 使能捕获中断 |
| `TIM3->SR` | CC1IF | 清 0 | 清除捕获标志 |
| `TIM3->CR1` | CEN | 1 | 启动计数器 |

---

## 14. 扩展练习

本课完成后，你可以尝试：

1. **改变频率**：把 `TIM2->ARR` 改成 `500 - 1`（2kHz），重新验证 LED 是否亮
2. **下降沿捕获**：把 `CCER` 的 `CC1P` 设为 1，改成下降沿捕获，周期值是否相同
3. **半周期测量**：配置通道 1 上升沿捕获、通道 2 下降沿捕获，测脉宽（不仅周期）
4. **去除 TIM2**：用函数发生器从 PA6 输入一个已知频率信号，验证捕获结果

---

## 15. 下一课预告

下一课建议进入：

- `10_adc_polling`

因为到目前为止，你已经把数字输入、输出、定时、PWM、中断、测频这些基础链路打通了。

下一步就可以开始进入：

- **模拟量采样**
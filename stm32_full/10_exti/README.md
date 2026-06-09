# 10_exti - 外部中断 EXTI

## 1. 本课到底在学什么

本课表面现象是：PA0 接一个按键，按键另一端接 GND。每按下一次按键，PC13 板载 LED 的状态翻转一次。

真正要学的是外部中断链路。一个引脚上的电平变化，不是自动就能让 CPU 执行函数；它要先进入 GPIO 输入电路，再通过 AFIO 选择连接到哪条 EXTI 线，再由 EXTI 判断边沿、置 pending 标志、向 NVIC 发请求，最后 CPU 才跳进 `EXTI0_IRQHandler()`。

这节课接在 GPIO 按键和定时器课程之后。前面的按键读取更像“主循环反复问 PA0 现在是不是低电平”；本课换成“PA0 出现下降沿时硬件主动打断 CPU”。这是后续学习中断、输入捕获、串口中断、FreeRTOS 中断交互的基础。

## 2. 本课学习目标

学完本课，你应该能做到：

- 解释 PA0 按键接 GND 时为什么要配置内部上拉。
- 说明按下按键为什么是下降沿，而不是上升沿。
- 说清楚 GPIO 输入、AFIO 映射、EXTI 线、NVIC、ISR 各自负责哪一步。
- 解释为什么 EXTI0 同一时间只能来自 PA0/PB0/PC0 等同编号引脚中的一个。
- 看懂 `EXTI->IMR`、`FTSR`、`RTSR`、`PR` 的不同作用。
- 解释 `EXTI->PR = EXTI_PR_PR0` 为什么是写 1 清 pending。
- 说明 HAL 版 `GPIO_MODE_IT_FALLING` 底层封装了哪些寄存器配置。
- 解释 `EXTI0_IRQHandler()`、`HAL_GPIO_EXTI_IRQHandler()`、`HAL_GPIO_EXTI_Callback()` 三者的调用关系。
- 能根据“按下无反应、一直进中断、按下触发多次”等现象定位问题。

## 3. 本课目录结构

```text
10_exti/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

`reg/` 直接配置 RCC、GPIOA、GPIOC、AFIO、EXTI 和 NVIC。

`hal/` 使用 HAL 的 GPIO 中断模式配置 PA0，并通过 HAL 回调翻转 PC13。

两份 `platformio.ini` 都使用 `genericSTM32F103C8`、`stm32cube`、`stlink`，并定义 `HSE_VALUE=8000000U`。

## 4. 实验硬件

本课使用：

- STM32F103C8T6 BluePill
- ST-Link 下载器
- PA0 外接按键
- PC13 板载 LED

按键推荐接法：

```text
PA0 ---- 按键 ---- GND
```

PA0 使用内部上拉。未按下时，PA0 被内部上拉保持为高电平；按下时，PA0 被按键接到 GND，变成低电平。因此“按下”对应 PA0 从高到低，也就是下降沿。

PC13 是板载 LED 输出脚。BluePill 上 PC13 LED 常见接法是低电平点亮、高电平熄灭，所以代码初始化时先写高电平，让 LED 先熄灭。

## 5. 先建立一个最基本的脑图

本课按六层拆开看。

现象层：按下 PA0 按键，PC13 LED 翻转。由于机械按键有抖动，一次按下可能触发多次，这不是 EXTI 必然配置错，而是按键物理触点反复弹跳。

物理/硬件层：PA0 通过按键接 GND，内部上拉提供默认高电平。按下时 PA0 电压从高变低，形成下降沿。PC13 作为输出脚驱动板载 LED。

芯片模块层：GPIOA 负责 PA0 输入模式，GPIOC 负责 PC13 输出模式，AFIO 负责把 EXTI0 的来源选择为 PA0，EXTI 负责边沿检测和 pending 标志，NVIC 负责让 Cortex-M3 接收 EXTI0 中断。

寄存器/bit 层：`GPIOA->CRL` 配置 PA0 输入上拉，`GPIOA->BSRR` 让 `ODR0=1` 选择上拉，`AFIO->EXTICR[0]` 选择 EXTI0 来源，`EXTI->IMR` 放行中断，`EXTI->FTSR` 选择下降沿，`EXTI->RTSR` 关闭上升沿，`EXTI->PR` 清 pending，NVIC 使能 `EXTI0_IRQn`。

C/CMSIS 层：寄存器版通过 `EXTI0_IRQHandler()` 提供中断入口，通过 `NVIC_SetPriority()` 和 `NVIC_EnableIRQ()` 配置内核中断控制器，通过 `GPIOC->ODR`、`BSRR`、`BRR` 翻转 LED。

HAL/工程层：HAL 版用 `GPIO_MODE_IT_FALLING` 表达“输入 + 中断 + 下降沿”，用 `GPIO_PULLUP` 表达内部上拉，用 `HAL_GPIO_EXTI_IRQHandler()` 处理中断标志，再进入 `HAL_GPIO_EXTI_Callback()` 写业务代码。

完整链路是：

1. 系统时钟配置为 72MHz。
2. GPIOC 时钟打开，PC13 配成推挽输出，初始写高电平。
3. GPIOA 时钟打开，PA0 配成输入上拉。
4. AFIO 时钟打开，让 EXTI 端口映射寄存器可用。
5. `AFIO->EXTICR[0]` 把 EXTI0 来源选为 PA0。
6. `EXTI->IMR` 允许 EXTI0 产生中断请求。
7. `EXTI->FTSR` 打开下降沿触发。
8. `EXTI->RTSR` 关闭上升沿触发。
9. `EXTI->PR` 先清一次可能残留的 pending。
10. NVIC 设置 `EXTI0_IRQn` 优先级并使能。
11. 按键按下，PA0 产生下降沿。
12. EXTI0 置 pending 并向 NVIC 发中断请求。
13. CPU 进入 `EXTI0_IRQHandler()`。
14. ISR 清除 `PR0`，再翻转 PC13。

## 6. 核心名词解释

### 6.1 `EXTI` 是什么

`EXTI` 是 External Interrupt/Event Controller，中文叫外部中断/事件控制器。

它属于 STM32 片上外设层。它的工作是监视外部中断线上的边沿变化，在满足触发条件时置位 pending 标志，并可向 NVIC 发中断请求。

本课出现 EXTI，是因为 PA0 按键不再由主循环轮询，而是通过下降沿主动触发 CPU 进入中断。

如果 EXTI 没配置，PA0 电平变化只停留在 GPIO 输入状态，不会自动进入 `EXTI0_IRQHandler()`。

### 6.2 `EXTI0` 是什么

`EXTI0` 是 EXTI 的 0 号外部中断线。

它属于 EXTI 线层。STM32F1 的 GPIO 外部中断按引脚编号分线：0 号引脚对应 EXTI0，1 号引脚对应 EXTI1。PA0、PB0、PC0 都可以作为 EXTI0 来源，但同一时间只能选一个端口。

本课 PA0 是 0 号引脚，所以使用 EXTI0，并对应 NVIC 中断号 `EXTI0_IRQn`。

如果你把按键接到 PA1，却仍然配置 EXTI0，按键不会按预期触发，因为 PA1 应该走 EXTI1。

### 6.3 `PA0` 输入上拉是什么

PA0 输入上拉表示 PA0 作为输入脚，同时芯片内部接了一个上拉电阻，让它在按键松开时保持高电平。

它属于 GPIO 引脚电气层。按键另一端接 GND，所以按下时外部把 PA0 拉低。

寄存器版中，F1 的输入上拉/下拉由两步决定：`CNF0 = 10` 选择上拉/下拉输入，`ODR0 = 1` 选择上拉。

如果没有上拉，按键松开时 PA0 会悬空，电平不稳定，可能乱触发。

### 6.4 下降沿是什么

下降沿就是电平从高变低的瞬间。

它属于信号边沿层。本课 PA0 松开为高，按下接地为低，所以按下动作产生下降沿。

代码中用 `EXTI->FTSR |= EXTI_FTSR_TR0` 选择下降沿触发，HAL 版用 `GPIO_MODE_IT_FALLING` 表达同样意思。

如果配置成上升沿，常见现象是按下不翻转，松开时才翻转。

### 6.5 `AFIO` 是什么

`AFIO` 是 Alternate Function I/O，中文叫复用功能 I/O。

它属于 STM32F1 的复用和映射控制层。EXTI 线到底来自哪个 GPIO 端口，要通过 AFIO 的 `EXTICR` 寄存器选择。

本课中 EXTI0 要来自 PA0，所以需要打开 AFIO 时钟并配置 `AFIO->EXTICR[0]`。

如果 AFIO 时钟没开或映射错，PA0 的边沿可能不会连接到 EXTI0。

### 6.6 `AFIO->EXTICR[0]` 是什么

`EXTICR` 是 External Interrupt Configuration Register，中文叫外部中断配置寄存器。

它属于 AFIO 寄存器层。`EXTICR[0]` 管 EXTI0 到 EXTI3 的端口来源选择。EXTI0 字段写 `0000` 表示 PA0，写 `0001` 表示 PB0，写 `0010` 表示 PC0。

本课代码：

```c
AFIO->EXTICR[0] &= ~AFIO_EXTICR1_EXTI0;
```

清零不是没做事，而是明确让 EXTI0 选择 A 端口。

### 6.7 `EXTI->IMR` 是什么

`IMR` 是 Interrupt Mask Register，中文叫中断屏蔽寄存器。

它属于 EXTI 中断请求开关层。`MR0 = 1` 表示 EXTI0 的 pending 可以产生中断请求；`MR0 = 0` 表示即使检测到边沿，也不向 NVIC 发送中断请求。

本课代码：

```c
EXTI->IMR |= EXTI_IMR_MR0;
```

如果只配置边沿，不打开 `IMR`，CPU 不会进入中断。

### 6.8 `EXTI->FTSR` 是什么

`FTSR` 是 Falling Trigger Selection Register，中文叫下降沿触发选择寄存器。

它属于 EXTI 触发条件层。`TR0 = 1` 表示 EXTI0 对下降沿敏感。

本课代码：

```c
EXTI->FTSR |= EXTI_FTSR_TR0;
```

如果漏掉它，按下 PA0 的下降沿不会触发 EXTI0 pending。

### 6.9 `EXTI->RTSR` 是什么

`RTSR` 是 Rising Trigger Selection Register，中文叫上升沿触发选择寄存器。

它属于 EXTI 触发条件层。本课代码：

```c
EXTI->RTSR &= ~EXTI_RTSR_TR0;
```

这表示不使用上升沿，避免松开按键时也触发。

如果同时打开 `FTSR` 和 `RTSR`，按下和松开都可能触发，机械抖动时翻转次数会更多。

### 6.10 `EXTI->PR` 是什么

`PR` 是 Pending Register，中文叫挂起寄存器。

它属于 EXTI 状态标志层。当 EXTI0 检测到有效边沿后，`PR0` 会置位，表示这条线有待处理事件。软件需要写 1 清除它。

本课代码：

```c
EXTI->PR = EXTI_PR_PR0;
```

如果 ISR 不清 `PR0`，CPU 可能退出中断后立刻再次进入，表现为一直卡在中断里。

### 6.11 `NVIC` 是什么

`NVIC` 是 Nested Vectored Interrupt Controller，中文叫嵌套向量中断控制器。

它属于 Cortex-M3 内核中断控制层。EXTI 外设可以发请求，但 CPU 是否接收、优先级是多少，由 NVIC 控制。

本课代码：

```c
NVIC_SetPriority(EXTI0_IRQn, 1U);
NVIC_EnableIRQ(EXTI0_IRQn);
```

如果 EXTI 配好了但 NVIC 没使能，`PR0` 可能置位，但 CPU 不会进入 `EXTI0_IRQHandler()`。

### 6.12 `EXTI0_IRQn` 是什么

`EXTI0_IRQn` 是 EXTI0 在 NVIC 中的中断号。

它属于 CMSIS 中断枚举层。`NVIC_EnableIRQ()` 需要这个编号来知道打开哪个中断入口。

本课 PA0 对应 EXTI0，所以使用 `EXTI0_IRQn`。如果是 EXTI1，就要用 `EXTI1_IRQn`；如果是 EXTI5 到 EXTI9，则共享 `EXTI9_5_IRQn`。

### 6.13 `EXTI0_IRQHandler()` 是什么

`EXTI0_IRQHandler()` 是 EXTI0 的中断服务函数。

它属于 C 代码和启动文件中断向量层。函数名必须和启动文件向量表中的弱符号一致，CPU 进入 EXTI0 中断时才会跳到这里。

本课在这个函数里检查 `PR0`、清 pending、翻转 PC13。

如果函数名写错，编译可能通过，但中断会进入默认处理函数，表现为程序卡住或无响应。

### 6.14 `GPIO_MODE_IT_FALLING` 是什么

`GPIO_MODE_IT_FALLING` 是 HAL 的 GPIO 模式宏，中文可理解为下降沿中断输入模式。

它属于 HAL 软件封装层。它一次表达：该引脚作为输入、连接 EXTI 中断、下降沿触发。

HAL 版代码：

```c
gpio.Mode = GPIO_MODE_IT_FALLING;
```

它底层会参与配置 GPIO 输入、AFIO EXTI 映射、EXTI 下降沿和中断屏蔽。

### 6.15 `HAL_GPIO_EXTI_IRQHandler()` 是什么

`HAL_GPIO_EXTI_IRQHandler()` 是 HAL 的 EXTI 通用处理函数。

它属于 HAL 中断处理层。用户的 `EXTI0_IRQHandler()` 进入后，把 `GPIO_PIN_0` 传给它。它会检查对应 pending、清 pending，然后调用回调函数。

如果 HAL 版中断入口里不调用它，`HAL_GPIO_EXTI_Callback()` 不会自动执行。

### 6.16 `HAL_GPIO_EXTI_Callback()` 是什么

`HAL_GPIO_EXTI_Callback()` 是 HAL 提供给用户重写的回调函数。

它属于 HAL 用户业务层。HAL 清完 EXTI pending 后，会调用这个函数，并把触发的 GPIO pin 传进来。

本课代码：

```c
if (GPIO_Pin == GPIO_PIN_0) {
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
}
```

如果多个 EXTI 共用回调，不判断 `GPIO_Pin` 就可能把别的引脚触发也当成 PA0 处理。

## 7. 寄存器版代码逐步讲解

### 7.1 `system_clock_72mhz_init()` 建立运行时钟

代码先打开 HSE，等待 `HSERDY`，再配置 PLL x9，最后切换 SYSCLK 到 PLL。

EXTI 本身不是靠 72MHz 才能检测按键，但统一系统时钟能让整套工程和前后课程保持一致。Flash 等待周期也通过 `FLASH->ACR` 配好，避免 72MHz 下取指不稳定。

### 7.2 `led_pc13_init()` 打开 GPIOC 并配置 PC13

代码：

```c
RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;
GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
GPIOC->CRH |= GPIO_CRH_MODE13_1;
GPIOC->BSRR = GPIO_BSRR_BS13;
```

PC13 在 GPIOC 的 13 号引脚，所以配置 `CRH`。`MODE13_1` 让它成为输出，`CNF13` 清零表示通用推挽。

最后写 `BSRR_BS13` 把 PC13 置高。对 BluePill 板载 LED，通常高电平是熄灭。

### 7.3 `key_pa0_input_pullup_init()` 打开 GPIOA 时钟

代码：

```c
RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;
```

PA0 属于 GPIOA，配置 `GPIOA->CRL` 前必须开 GPIOA 时钟。否则你写了模式寄存器，硬件不会按预期工作。

### 7.4 PA0 配成输入上拉/下拉模式

代码：

```c
GPIOA->CRL &= ~(GPIO_CRL_MODE0 | GPIO_CRL_CNF0);
GPIOA->CRL |= GPIO_CRL_CNF0_1;
```

PA0 在 `CRL`。`MODE0 = 00` 表示输入模式，`CNF0 = 10` 表示上拉/下拉输入模式。

这里只是选择“上拉/下拉模式”，还没有决定到底上拉还是下拉。

### 7.5 通过 `ODR0=1` 选择内部上拉

代码：

```c
GPIOA->BSRR = GPIO_BSRR_BS0;
```

在 STM32F1 中，输入上拉/下拉模式下，`ODR0 = 1` 表示上拉，`ODR0 = 0` 表示下拉。

`BSRR_BS0` 会把 ODR0 置 1。硬件后果是 PA0 松开时默认读到高电平。

### 7.6 `exti0_init()` 打开 AFIO 时钟

代码：

```c
RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;
```

EXTI 的端口来源选择属于 AFIO。要写 `AFIO->EXTICR[0]`，必须先打开 AFIO 时钟。

如果漏掉这步，PA0 到 EXTI0 的映射配置不可靠。

### 7.7 把 EXTI0 映射到 PA0

代码：

```c
AFIO->EXTICR[0] &= ~AFIO_EXTICR1_EXTI0;
```

`EXTICR[0]` 中 EXTI0 字段为 `0000` 表示 A 端口。清零就是选择 PA0。

这一步解决的是“EXTI0 从哪个端口的 0 号引脚来”。它不是选择下降沿，也不是打开中断。

### 7.8 打开 EXTI0 中断请求

代码：

```c
EXTI->IMR |= EXTI_IMR_MR0;
```

`MR0 = 1` 允许 EXTI0 pending 向 NVIC 发送中断请求。

如果 `IMR` 没开，`PR0` 即使因为下降沿置位，也不会让 CPU 进入 ISR。

### 7.9 设置下降沿并关闭上升沿

代码：

```c
EXTI->FTSR |= EXTI_FTSR_TR0;
EXTI->RTSR &= ~EXTI_RTSR_TR0;
```

`FTSR.TR0 = 1` 表示 PA0 从高到低触发。`RTSR.TR0 = 0` 表示 PA0 从低到高不触发。

这对应本课接法：按下触发，松开不触发。

### 7.10 启动前先清 pending

代码：

```c
EXTI->PR = EXTI_PR_PR0;
```

如果配置过程中或上电后已经残留 pending，NVIC 一使能就可能立刻进中断。启动前写 1 清一次，可以让实验从干净状态开始。

### 7.11 配置 NVIC

代码：

```c
NVIC_SetPriority(EXTI0_IRQn, 1U);
NVIC_EnableIRQ(EXTI0_IRQn);
```

第一句设置中断优先级，第二句在 NVIC 中放行 EXTI0。

这一步属于内核中断控制，不属于 EXTI 外设内部。EXTI 和 NVIC 两边都要通，CPU 才能进入 ISR。

### 7.12 `EXTI0_IRQHandler()` 判断 pending

代码：

```c
if ((EXTI->PR & EXTI_PR_PR0) != 0U) {
```

进入 ISR 后先判断 `PR0`，确认确实是 EXTI0 pending。对于 EXTI0 单独中断，这个判断仍然是好习惯；对于共享中断线更是必须。

### 7.13 写 1 清 pending

代码：

```c
EXTI->PR = EXTI_PR_PR0;
```

EXTI pending 位是写 1 清除。这里不是写 0。清 pending 后，CPU 退出 ISR 才不会因为同一个事件再次进入。

### 7.14 翻转 PC13

代码：

```c
if ((GPIOC->ODR & GPIO_ODR_ODR13) != 0U) {
    GPIOC->BRR = GPIO_BRR_BR13;
} else {
    GPIOC->BSRR = GPIO_BSRR_BS13;
}
```

先读 PC13 当前输出状态。如果当前为高，就写 `BRR` 拉低；如果当前为低，就写 `BSRR` 拉高。

业务动作放在 ISR 里，所以每次有效按键边沿都会触发一次翻转。

### 7.15 主循环为什么为空

代码：

```c
while (1) {
}
```

主循环不轮询按键。按键事件由 EXTI 硬件触发，中断入口处理。

这正是本课和普通 GPIO 按键轮询的区别。

## 8. HAL 版代码逐步讲解

### 8.1 `HAL_Init()` 建立 HAL 基础

HAL 版主函数先调用：

```c
HAL_Init();
```

它初始化 HAL 状态和基础 tick。虽然本课主循环不用 `HAL_Delay()`，但 HAL 工程通常从这里开始。

### 8.2 HAL 版系统时钟配置

`RCC_OscInitTypeDef` 配置 HSE 和 PLL，`RCC_ClkInitTypeDef` 配置 SYSCLK、HCLK、PCLK1、PCLK2。

这些字段对应寄存器版的 RCC/FLASH 配置。目标仍是 72MHz。

### 8.3 `led_pc13_init()` 配置 PC13 输出

HAL 版：

```c
__HAL_RCC_GPIOC_CLK_ENABLE();
gpio.Pin = GPIO_PIN_13;
gpio.Mode = GPIO_MODE_OUTPUT_PP;
gpio.Pull = GPIO_NOPULL;
gpio.Speed = GPIO_SPEED_FREQ_LOW;
HAL_GPIO_Init(GPIOC, &gpio);
HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
```

`__HAL_RCC_GPIOC_CLK_ENABLE()` 对应开 GPIOC 时钟。`GPIO_MODE_OUTPUT_PP` 对应通用推挽输出。`HAL_GPIO_WritePin()` 对应写输出电平。

### 8.4 `key_pa0_exti_init()` 打开 GPIOA 时钟

代码：

```c
__HAL_RCC_GPIOA_CLK_ENABLE();
```

PA0 属于 GPIOA。HAL 也不会替你凭空打开所有 GPIO 时钟，配置引脚前必须打开对应端口时钟。

### 8.5 `gpio.Pin = GPIO_PIN_0`

这表示当前 `GPIO_InitTypeDef` 配置对象作用在 0 号引脚。

因为 `HAL_GPIO_Init(GPIOA, &gpio)` 的第一个参数是 GPIOA，所以完整含义是 PA0。

### 8.6 `GPIO_MODE_IT_FALLING`

代码：

```c
gpio.Mode = GPIO_MODE_IT_FALLING;
```

它表达输入中断下降沿模式。底层对应 GPIO 输入模式、EXTI 下降沿触发、EXTI 中断请求等配置。

这一个 HAL 字段背后包含寄存器版的多步配置，不是普通 GPIO 输入。

### 8.7 `GPIO_PULLUP`

代码：

```c
gpio.Pull = GPIO_PULLUP;
```

它对应内部上拉。底层在 STM32F1 上会落到输入上拉/下拉模式和 ODR 选择上拉。

如果写成 `GPIO_NOPULL`，PA0 松开会悬空；如果写成 `GPIO_PULLDOWN`，按键接 GND 时按下前后都可能读低，下降沿不明显。

### 8.8 `HAL_GPIO_Init(GPIOA, &gpio)`

这一步把 PA0 的输入上拉、EXTI 下降沿、中断模式落到硬件寄存器。

对于 EXTI 模式，HAL 不只写 GPIO 模式，还会处理 AFIO EXTI 映射和 EXTI 触发配置。

### 8.9 HAL NVIC 配置

代码：

```c
HAL_NVIC_SetPriority(EXTI0_IRQn, 1, 0);
HAL_NVIC_EnableIRQ(EXTI0_IRQn);
```

这对应寄存器版 `NVIC_SetPriority()` 和 `NVIC_EnableIRQ()`。

GPIO/EXTI 配好了，但 NVIC 没开，CPU 仍然进不了 `EXTI0_IRQHandler()`。

### 8.10 HAL 版 `EXTI0_IRQHandler()`

代码：

```c
void EXTI0_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0);
}
```

HAL 版中断入口不直接翻转 LED，而是先交给 HAL 通用处理函数。它会检查并清除 EXTI pending。

### 8.11 `HAL_GPIO_EXTI_Callback()`

代码：

```c
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_0) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    }
}
```

这是用户业务层。HAL 处理完中断标志后，把触发的 pin 传给回调。这里判断确实是 `GPIO_PIN_0`，再翻转 PC13。

### 8.12 `error_handler()` 的作用

如果系统时钟配置失败，HAL 版进入：

```c
__disable_irq();
while (1) {
}
```

这能避免系统在时钟失败后继续跑。若 HAL 版完全无现象，可以用调试器看是否停在这里。

## 9. 两个版本真正应该怎么学

寄存器版重点抓四个门：

```text
GPIO 输入门：PA0 输入上拉
AFIO 映射门：EXTI0 来自 PA0
EXTI 触发门：IMR + FTSR + PR
NVIC CPU 门：EXTI0_IRQn 使能
```

HAL 版重点抓调用链：

```text
HAL_GPIO_Init 配置 PA0/EXTI
HAL_NVIC_EnableIRQ 放行 CPU 中断
EXTI0_IRQHandler 进入中断入口
HAL_GPIO_EXTI_IRQHandler 清 pending
HAL_GPIO_EXTI_Callback 执行业务
```

只要你能把 `GPIO_MODE_IT_FALLING` 拆回输入上拉、AFIO 映射、下降沿、IMR，HAL 就不是黑箱。

## 10. 检验问题清单

### 10.1 为什么 PA0 接 GND 的按键要用内部上拉？

答：按键松开时 PA0 没有外部驱动，如果不用上拉就会悬空。内部上拉让松开稳定为高，按下接 GND 稳定为低，才能形成清晰下降沿。

### 10.2 为什么按下触发要选下降沿？

答：本课松开为高，按下为低。按下瞬间电平从高到低，所以是下降沿。若选上升沿，则通常松开时触发。

### 10.3 EXTI0 为什么不能同时来自 PA0 和 PB0？

答：EXTI0 是 0 号线，同一时间只能通过 `AFIO->EXTICR[0]` 选择一个端口来源。PA0、PB0、PC0 都争用 EXTI0。

### 10.4 只打开 `FTSR`，不开 `IMR` 会进中断吗？

答：不会。`FTSR` 只选择下降沿触发条件，`IMR` 才允许 pending 向 NVIC 发中断请求。

### 10.5 开了 `IMR`，但没开 NVIC 会怎样？

答：EXTI 可能置 pending，也可能发出外设中断请求，但 Cortex-M3 的 NVIC 没放行，CPU 不会跳到 `EXTI0_IRQHandler()`。

### 10.6 `PR0` 为什么写 1 清除？

答：EXTI pending 位采用写 1 清除机制。写 0 不会清掉 pending。如果 ISR 不清，可能反复进入中断。

### 10.7 HAL 版为什么要在回调里判断 `GPIO_Pin`？

答：HAL 的 EXTI 回调可以被多个引脚共用。判断 `GPIO_Pin == GPIO_PIN_0` 能保证只有 PA0 的中断才翻转 PC13。

### 10.8 一次按下为什么可能翻转多次？

答：机械按键有抖动，按下瞬间触点可能多次接通/断开，形成多个下降沿。本课暂不消抖，所以可能多次触发。

## 11. 工程实现步骤

### 11.1 需求分析

需求是：PA0 按键按下时，PC13 翻转一次。

这要求 PA0 有稳定默认电平，按下能形成下降沿；EXTI0 能收到 PA0 的下降沿；NVIC 能把 EXTI0 送进 CPU；ISR 能清标志并执行 LED 翻转。

### 11.2 硬件核查

检查 PA0 是否通过按键接 GND。不要把按键接到 3.3V 后仍然使用下降沿逻辑，否则触发方向会变化。

检查开发板 GND、按键 GND 是否共地。PC13 是板载 LED，不需要额外接线。

### 11.3 寄存器路线

寄存器版按这个顺序实现：

1. 配置系统时钟。
2. 配置 PC13 输出并先熄灭 LED。
3. 配置 PA0 输入上拉。
4. 打开 AFIO 时钟。
5. `EXTICR[0]` 选择 EXTI0 来自 PA0。
6. `IMR.MR0 = 1`。
7. `FTSR.TR0 = 1`。
8. `RTSR.TR0 = 0`。
9. 写 1 清 `PR0`。
10. 配置并使能 `EXTI0_IRQn`。
11. 在 `EXTI0_IRQHandler()` 中清 pending 并翻转 PC13。

### 11.4 HAL 路线

HAL 版按这个顺序实现：

1. `HAL_Init()`。
2. 配置系统时钟。
3. 配置 PC13 为 `GPIO_MODE_OUTPUT_PP`。
4. 配置 PA0 为 `GPIO_MODE_IT_FALLING` 和 `GPIO_PULLUP`。
5. `HAL_NVIC_SetPriority(EXTI0_IRQn, 1, 0)`。
6. `HAL_NVIC_EnableIRQ(EXTI0_IRQn)`。
7. 在 `EXTI0_IRQHandler()` 调用 `HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0)`。
8. 在 `HAL_GPIO_EXTI_Callback()` 判断 pin 并翻转 PC13。

### 11.5 工程思维

中断适合处理“外部事件何时发生不确定”的场景。CPU 不必一直轮询 PA0，可以把时间留给别的任务，按键边沿出现时再进入 ISR。

但 ISR 应该短小。本课直接翻转 LED 是为了教学直观；更复杂工程里，常见做法是在 ISR 里置标志，主循环或任务里处理业务。

### 11.6 常见工程陷阱

第一个陷阱是只配 GPIO 输入，不配 AFIO/EXTI/NVIC，以为输入变化会自动进中断。

第二个陷阱是忘记清 `PR0`，导致反复进入中断。

第三个陷阱是边沿选反，按下不触发、松开才触发。

第四个陷阱是机械抖动。一次按下多个边沿是物理现象，需要消抖策略。

第五个陷阱是 HAL 版忘记在 IRQHandler 里调用 `HAL_GPIO_EXTI_IRQHandler()`，导致回调不执行。

## 12. 运行现象

下载寄存器版或 HAL 版后，PC13 初始熄灭。每次按下 PA0 按键，PC13 状态翻转。

如果你看到一次按下 LED 翻转多次，不要先怀疑 EXTI 配置。机械按键本身会抖动，本课故意没有加消抖，目的是让你看到真实边沿会被 EXTI 捕获。

## 13. 常见问题排查

### 13.1 按键完全无反应

先查 PA0 是否真的通过按键接 GND，再查 PA0 是否内部上拉。寄存器版看 `GPIOA->CRL` 和 `ODR0`，HAL 版看 `GPIO_PULLUP`。

然后查 AFIO 映射、`IMR.MR0`、`FTSR.TR0`、NVIC 使能。

### 13.2 按下不触发，松开触发

这是边沿选反的典型现象。

按键接 GND 且内部上拉时，按下是下降沿，松开是上升沿。检查 `FTSR` 和 `RTSR`，HAL 版检查是否用了 `GPIO_MODE_IT_RISING`。

### 13.3 程序一直进中断

重点查 ISR 是否清 `PR0`。EXTI pending 是写 1 清除，不是写 0。

如果清标志正确，还要检查 PA0 是否电平抖动严重或接线悬空。

### 13.4 HAL 版进了 IRQ 但不进回调

检查 `EXTI0_IRQHandler()` 里是否调用：

```c
HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0);
```

如果直接空着或传错 pin，HAL 不会调用正确回调。

### 13.5 一次按下翻转多次

这是按键抖动。可以加软件消抖，例如记录上一次触发时间，短时间内忽略新触发；也可以用硬件 RC 消抖。

本课先不消抖，是为了专注 EXTI 链路。

## 14. 本课最核心的结论

- EXTI 把 GPIO 引脚边沿变成中断请求。
- PA0 接 GND 按键需要内部上拉，按下对应下降沿。
- AFIO 决定 EXTI0 来自哪个端口的 0 号引脚。
- `IMR`、`FTSR/RTSR`、`PR` 分别负责中断放行、边沿选择、pending 状态。
- NVIC 是 CPU 是否接收 EXTI0 中断的最后入口。
- ISR 必须清 pending，否则可能反复进入。
- HAL 的 `GPIO_MODE_IT_FALLING` 封装了 GPIO 输入、EXTI 下降沿和中断模式，但 NVIC 和回调链仍要理解。

## 15. 建议你现在怎么读这节课

先把物理电平想清楚：松开高，按下低，所以按下是下降沿。

然后回到 `exti0_init()`，把每一句归类：AFIO 映射、EXTI 放行、边沿选择、pending 清除、NVIC 使能。

最后看 HAL 版，把 `GPIO_MODE_IT_FALLING` 拆成寄存器版的那几步，再顺着 `EXTI0_IRQHandler()` 读到 `HAL_GPIO_EXTI_Callback()`。

## 16. 扩展练习

1. 改成上升沿触发，观察松开按键时翻转。
2. 同时打开上升沿和下降沿，观察按下、松开都会触发。
3. 在 ISR 中只设置一个 `volatile` 标志，在主循环里翻转 LED。
4. 加一个 20ms 软件消抖，比较翻转次数变化。
5. 把按键换到 PA1，尝试改成 EXTI1。

## 17. 下一课预告

上一课：[09_pwm_advanced](../09_pwm_advanced/README.md)

下一课：[11_input_capture](../11_input_capture/README.md)

下一课会继续学习“外部信号进入芯片后被硬件记录”。不同的是，输入捕获不是只告诉 CPU 有边沿，而是让定时器把边沿发生时的 `CNT` 锁存到 `CCR` 中，用来测频率或脉宽。

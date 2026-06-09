# 07_timer_output_compare - TIM 输出比较

## 1. 本课到底在学什么

本课表面现象是：PA0 引脚不靠主循环反复写 GPIO，而是由 TIM2_CH1 自动输出翻转电平；PC13 仍然在主循环里闪烁，只用来证明程序还在运行。

真正要学的是 STM32 定时器的输出比较功能。上一课 `06_timer_base` 里，TIM2 溢出后产生中断，CPU 进入中断函数，再由软件翻转 LED。本课换成另一条链路：TIM2 的计数器 `CNT` 自己往上数，当 `CNT` 等于通道 1 的比较寄存器 `CCR1` 时，定时器内部产生比较匹配事件，然后按照 `OC1M` 配置直接改变 TIM2_CH1 的输出状态。

这就把“定时”和“输出动作”都放进了定时器硬件内部。CPU 只负责初始化；初始化完成后，PA0 的边沿由 TIM2 外设自动产生。

本课最核心的一句话是：

```text
CNT 提供时间轴，CCR1 提供比较点，OC1M 决定匹配时怎么改输出，CC1E 决定这个输出能不能送到 PA0。
```

## 2. 本课学习目标

学完本课，你应该能做到：

- 解释为什么 `CNT == CCR1` 时 PA0 会发生电平翻转。
- 说清楚 `PSC`、`ARR`、`CNT`、`CCR1` 在时间轴里的分工。
- 解释为什么 PA0 必须配置为复用推挽输出，而不是普通 GPIO 输出。
- 看懂 `CCMR1.OC1M = 011` 为什么表示输出比较 toggle 模式。
- 看懂 `CCER.CC1E` 为什么是“通道输出开关”。
- 区分定时器更新中断和定时器输出比较：前者让 CPU 做事，后者让定时器通道自己改输出。
- 把 HAL 版的 `TIM_OC_InitTypeDef` 字段反推回寄存器版的 `PSC`、`ARR`、`CCR1`、`OC1M`、`CC1E`。
- 知道本课的 PA0 方波为什么不是由 `GPIOA->ODR`、`BSRR`、`BRR` 直接写出来的。

## 3. 本课目录结构

```text
07_timer_output_compare/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

`reg/` 用寄存器直接配置 TIM2 输出比较。你会看到 `GPIOA->CRL`、`TIM2->PSC`、`TIM2->ARR`、`TIM2->CCR1`、`TIM2->CCMR1`、`TIM2->CCER` 这些底层对象。

`hal/` 用 HAL 接口完成同一件事。你会看到 `GPIO_MODE_AF_PP`、`HAL_TIM_OC_Init()`、`TIM_OC_InitTypeDef`、`HAL_TIM_OC_ConfigChannel()`、`HAL_TIM_OC_Start()` 这些封装接口。

这两份代码的目标完全相同：让 TIM2_CH1 通过 PA0 输出比较翻转信号。

## 4. 实验硬件

本课使用：

- STM32F103C8T6 BluePill
- ST-Link 下载器
- PA0 引脚
- PC13 板载 LED
- 示波器、逻辑分析仪，或外接 LED 加限流电阻

PA0 是 STM32F103 上 TIM2_CH1 的默认复用引脚。本课没有改 AFIO 重映射，所以 TIM2_CH1 默认就从 PA0 出来。

推荐用示波器或逻辑分析仪观察 PA0。因为本课的输出比较是定时器硬件边沿，波形用仪器看最直观。如果只接 LED，你只能看到很慢的亮灭变化，而且受 LED 接法影响，直觉上可能会误判高低电平。

PC13 仍然作为心跳灯。它和 PA0 的输出比较没有功能依赖：PC13 闪，只能说明主循环在跑；PA0 有没有波形，要看 TIM2、PA0 复用、通道输出是否配置正确。

## 5. 先建立一个最基本的脑图

本课从肉眼现象拆到底层链路，可以分成六层。

现象层：PA0 会周期性改变电平，PC13 会在主循环里慢速闪烁。PA0 的改变不是主循环写出来的，而是 TIM2_CH1 的硬件输出。

物理/硬件层：PA0 是芯片封装上的一个真实引脚。要让 TIM2_CH1 控制 PA0，PA0 不能只当普通 GPIO，它必须进入复用功能输出状态。这样引脚输出驱动不再听 `GPIOA->ODR` 的普通输出值，而是接到 TIM2_CH1 的输出信号。

芯片模块层：RCC 给 GPIOA、AFIO、TIM2 送时钟；GPIOA 管 PA0 的输出模式；TIM2 负责计数、比较和通道输出；AFIO 负责 F1 系列复用功能映射相关能力。

寄存器/bit 层：`PSC` 把 72MHz 定时器时钟分频成 10kHz；`ARR` 决定计数周期；`CCR1` 决定比较点；`CCMR1.OC1M` 决定匹配时执行 toggle；`CCER.CC1E` 打开通道输出；`CR1.CEN` 启动计数器。

C/CMSIS 层：代码通过 `TIM2->PSC`、`TIM2->ARR`、`TIM2->CCR1` 这样的结构体指针写寄存器，通过 `TIM_CCMR1_OC1M_0`、`TIM_CCER_CC1E` 这样的宏定位 bit。

HAL/工程层：HAL 版用 `TIM_HandleTypeDef` 表示 TIM2，用 `TIM_OC_InitTypeDef` 表示通道输出比较参数，用 `HAL_TIM_OC_Start()` 把“配置好了的通道”真正启动起来。

完整执行链路是：

1. 系统时钟配置到 72MHz。
2. RCC 打开 GPIOA 和 TIM2 的外设时钟。
3. PA0 配成复用推挽输出，让 TIM2_CH1 能驱动引脚。
4. TIM2 设置 `PSC = 7200 - 1`，得到 10kHz 计数频率。
5. TIM2 设置 `ARR = 10000 - 1`，计数器每 1 秒从 0 走到 9999 再更新。
6. TIM2 设置 `CCR1 = 5000`，通道 1 在计数到 5000 时产生比较匹配。
7. TIM2 设置 `OC1M = 011`，比较匹配时翻转通道输出。
8. TIM2 设置 `CC1E = 1`，允许通道 1 的输出送到外部引脚。
9. TIM2 设置 `CEN = 1`，计数器开始运行。
10. 之后每当 `CNT == CCR1`，TIM2 硬件自动翻转 PA0。

注意一个细节：本课没有在匹配后修改 `CCR1`。所以每个 `ARR` 周期里只在 `CNT = 5000` 这一点翻转一次。`ARR` 周期是 1 秒，PA0 每 1 秒翻转一次电平，完整高低电平周期大约是 2 秒。

## 6. 核心名词解释

### 6.1 `TIM2_CH1` 是什么

`TIM2_CH1` 的中文可以叫 TIM2 定时器通道 1。

它属于芯片外设层。TIM2 是一个通用定时器，CH1 是 TIM2 内部的第 1 个通道。通道不是一个普通变量，而是定时器内部的一组比较、捕获、输出控制电路。

本课用的是 CH1 的输出比较功能。TIM2 的计数器 `CNT` 一直计数，CH1 拿 `CNT` 和 `CCR1` 比较。如果二者相等，CH1 就产生比较匹配事件。

它在代码里体现为：

```c
TIM2->CCR1 = 5000U;
TIM2->CCMR1 |= TIM_CCMR1_OC1M_0 | TIM_CCMR1_OC1M_1;
TIM2->CCER |= TIM_CCER_CC1E;
```

如果通道选错，比如 HAL 里配置了 `TIM_CHANNEL_2`，而硬件仍然接 PA0，那么 PA0 不会按预期输出。因为 PA0 默认接的是 TIM2_CH1，不是随便哪个通道都能从 PA0 出来。

### 6.2 `PA0` 是什么

`PA0` 是 GPIOA 端口的第 0 号引脚。

它属于物理引脚层和 GPIO 外设层。物理上它是芯片封装上的一个脚；在芯片内部，它先归 GPIOA 管理，同时也可以通过复用功能连接到 TIM2_CH1。

本课里 PA0 不是普通输出口。普通输出口的电平通常由 `GPIOA->ODR`、`BSRR`、`BRR` 控制；本课的 PA0 要输出 TIM2_CH1 信号，所以它必须配置成复用推挽输出。

寄存器版代码是：

```c
GPIOA->CRL &= ~(GPIO_CRL_MODE0 | GPIO_CRL_CNF0);
GPIOA->CRL |= GPIO_CRL_MODE0_1 | GPIO_CRL_CNF0_1;
```

HAL 版代码是：

```c
gpio.Pin = GPIO_PIN_0;
gpio.Mode = GPIO_MODE_AF_PP;
HAL_GPIO_Init(GPIOA, &gpio);
```

如果 PA0 配成普通推挽输出，TIM2 内部比较可能正常发生，但通道信号没有正确接到引脚输出路径，外部就看不到预期波形。

### 6.3 复用推挽输出是什么

复用推挽输出就是 GPIO 引脚的输出信号来源不是普通 GPIO 输出寄存器，而是某个片上外设；同时输出驱动方式是推挽，可以主动拉高和主动拉低。

它属于 GPIO 模式配置层。

在 STM32F1 里，每个 GPIO 引脚的 4 个配置 bit 分成 `MODE` 和 `CNF` 两部分。PA0 在 `GPIOA->CRL` 里配置，因为 0 到 7 号引脚属于 CRL。`MODE0 = 10` 表示输出速度 2MHz，`CNF0 = 10` 表示复用推挽输出。

本课出现它，是因为 TIM2_CH1 要接管 PA0。只有 GPIO 模式允许外设输出，定时器通道的输出信号才有机会到达芯片引脚。

如果这里写成输入模式，PA0 不会主动输出波形。如果写成普通推挽输出，PA0 主要听 GPIO 输出数据寄存器，不是本课要的定时器通道输出。

### 6.4 `AFIO` 是什么

`AFIO` 是 Alternate Function I/O，中文可以叫复用功能 I/O。

它属于芯片复用功能控制层。在 STM32F1 中，引脚复用、重映射、外部中断端口选择等功能和 AFIO 模块有关。

寄存器版里有：

```c
RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN;
```

本课没有进行 TIM2 重映射，TIM2_CH1 默认在 PA0。但寄存器版仍然打开 AFIO 时钟，是一种稳妥写法：当课程进入复用功能输出时，把复用功能模块也明确打开，避免后续加入重映射或相关配置时没有时钟。

HAL 版没有显式写 `__HAL_RCC_AFIO_CLK_ENABLE()`，因为代码没有配置重映射，PA0 是 TIM2_CH1 默认映射。不要把这理解成 HAL 脱离了复用功能；HAL 只是通过 `GPIO_MODE_AF_PP` 表达“这个脚作为外设复用输出”。

### 6.5 `PSC` 是什么

`PSC` 是 Prescaler，中文叫预分频器。

它属于 TIM2 定时器时基层。它控制定时器计数器的输入节拍有多快。定时器时钟进来以后，先经过 `PSC + 1` 分频，再推动 `CNT` 加 1。

本课系统时钟配置后，TIM2 的计数时钟按课程代码理解为 72MHz。代码写：

```c
TIM2->PSC = 7200U - 1U;
```

硬件实际分频系数是 `PSC + 1`，所以这里是 7200 分频。72MHz / 7200 = 10kHz，也就是 `CNT` 每 0.1ms 加 1。

如果 `PSC` 写错，整个时间轴都会错。比如写成 `720 - 1`，计数频率会变成 100kHz，PA0 翻转节奏会快 10 倍。

### 6.6 `ARR` 是什么

`ARR` 是 Auto-Reload Register，中文叫自动重装载寄存器。

它属于 TIM2 时基层。向上计数时，`CNT` 从 0 开始加，计到 `ARR` 后发生更新，然后重新从 0 开始。

本课代码是：

```c
TIM2->ARR = 10000U - 1U;
```

因为 `CNT` 频率是 10kHz，`ARR = 9999` 表示一轮包含 10000 个计数，计数周期是 1 秒。

如果 `ARR` 太小，计数周期会变短，PA0 翻转也会更快。如果 `CCR1` 大于 `ARR`，`CNT` 永远数不到 `CCR1`，通道 1 就不会产生预期比较匹配。

### 6.7 `CNT` 是什么

`CNT` 是 Counter，中文叫计数器当前值。

它属于 TIM2 内部硬件计数层。它不是由主循环每次手动加一，而是由定时器时钟自动驱动。

本课没有直接写 `TIM2->CNT`，但整个输出比较都围绕它工作。`PSC` 决定 `CNT` 多久加 1，`ARR` 决定 `CNT` 数到哪里回到 0，`CCR1` 决定 `CNT` 数到哪里触发通道事件。

如果定时器没有启动，也就是 `CR1.CEN` 没置位，`CNT` 不跑，所有比较逻辑都没有时间轴，PA0 就不会按预期翻转。

### 6.8 `CCR1` 是什么

`CCR1` 是 Capture/Compare Register 1，中文叫捕获/比较寄存器 1。

它属于 TIM2_CH1 通道寄存器层。同一个寄存器在输入捕获和输出比较中用途不同：输入捕获时保存捕获到的计数值；输出比较时保存“要比较的目标值”。

本课代码是：

```c
TIM2->CCR1 = 5000U;
```

这表示当 `CNT` 计到 5000 时，通道 1 产生比较匹配。由于 `CNT` 每 0.1ms 加 1，所以 5000 对应一轮计数中的 0.5 秒位置。

如果 `CCR1` 改成 2500，比较点会提前到 0.25 秒位置。如果改成 7500，比较点会延后到 0.75 秒位置。如果改成大于 `ARR` 的值，本轮计数永远匹配不到，PA0 可能一直停在某个状态。

### 6.9 输出比较是什么

输出比较就是定时器通道把 `CNT` 和 `CCR` 比较，在匹配时按预设规则改变通道输出或产生事件。

它属于定时器通道功能层。

普通定时器中断的思路是“时间到了通知 CPU”。输出比较的思路是“时间到了通道自己做输出动作”。本课选择的输出动作是 toggle，也就是匹配时翻转输出。

本课出现输出比较，是为了从“CPU 软件翻转 LED”过渡到“定时器硬件生成边沿”。这对后面的 PWM 很重要，因为 PWM 本质上也是定时器根据比较值控制输出电平。

如果把输出比较模式配置错，可能出现匹配时置高、置低、保持不变，而不是翻转。

### 6.10 `CCMR1` 是什么

`CCMR1` 是 Capture/Compare Mode Register 1，中文叫捕获/比较模式寄存器 1。

它属于 TIM2 通道模式配置层。通道 1 和通道 2 的模式字段在 `CCMR1` 里。对于通道 1，输出比较模式主要看 `OC1M` 字段。

本课代码是：

```c
TIM2->CCMR1 &= ~TIM_CCMR1_OC1M;
TIM2->CCMR1 |= TIM_CCMR1_OC1M_0 | TIM_CCMR1_OC1M_1;
```

第一句先清掉 `OC1M` 旧值，第二句把它设置成 `011`。这就是“先清再设”的典型寄存器写法，避免旧 bit 残留导致模式不是你以为的值。

如果只用 `|=` 设置而不清位，旧配置可能叠加出另一个 `OC1M` 编码，匹配时行为就不确定。

### 6.11 `OC1M` 是什么

`OC1M` 是 Output Compare 1 Mode，中文叫输出比较 1 模式字段。

它属于 `CCMR1` 里的 bit 字段，控制 TIM2_CH1 在比较匹配时做什么。

本课设置：

```text
OC1M = 011
```

对应输出比较 toggle 模式。也就是每次 `CNT == CCR1`，通道输出从低变高，或从高变低。

如果 `OC1M` 设置成 frozen，匹配时输出不变；如果设置成 active/inactive，匹配时会强制到某个状态；如果设置成 PWM 模式，那就是下一课要学的按周期和占空比输出。

### 6.12 toggle mode 是什么

toggle mode 中文可以叫翻转模式。

它属于输出比较动作层。所谓翻转，不是固定写高，也不是固定写低，而是把当前输出状态取反。

本课使用 toggle 是为了让你直观看到“比较事件能改变引脚电平”。`CCR1 = 5000` 固定时，每个 1 秒计数周期只匹配一次，所以 PA0 每秒翻转一次，完整方波周期约 2 秒。

如果你以为 `CCR1 = 5000` 就会得到 0.5 秒翻转一次，那就是混淆了“比较点在 0.5 秒位置”和“每隔 0.5 秒产生一次事件”。本课没有在匹配后把 `CCR1` 加 5000，所以不会在同一周期内继续安排下一次比较。

### 6.13 `CCER` 是什么

`CCER` 是 Capture/Compare Enable Register，中文叫捕获/比较使能寄存器。

它属于 TIM2 通道输出开关和极性配置层。模式配置好了，不代表信号一定能送到引脚。`CCER` 里的使能位负责把通道输出真正打开。

本课代码是：

```c
TIM2->CCER |= TIM_CCER_CC1E;
```

如果没有这一步，TIM2 内部仍然可以计数，`CNT == CCR1` 时也可能产生比较事件，但 CH1 输出没有打开，PA0 外部就看不到对应波形。

### 6.14 `CC1E` 是什么

`CC1E` 是 Capture/Compare 1 Output Enable，中文叫通道 1 输出使能位。

它属于 `CCER` 中的 bit。它控制 TIM2_CH1 的输出是否启用。

本课必须设置它，因为我们不是只想在内部产生比较事件，而是要让事件改变 PA0 引脚电平。

如果 `CC1E = 0`，常见现象是：代码能跑，PC13 心跳正常，TIM2 也可能在计数，但 PA0 没有输出比较波形。这种故障很容易误判成 `CCR1` 或 `PSC` 算错，实际只是通道没开。

### 6.15 `EGR.UG` 是什么

`EGR` 是 Event Generation Register，中文叫事件产生寄存器。`UG` 是 Update Generation，中文叫软件产生更新事件。

它属于 TIM2 事件控制层。

本课代码是：

```c
TIM2->EGR = TIM_EGR_UG;
```

这句会主动产生一次更新事件，让预分频器等配置装载到硬件实际使用的影子逻辑中。对于定时器来说，有些配置不是写了寄存器就立刻以你直觉中的方式生效，更新事件是让时基配置进入一致状态的常用步骤。

如果少了它，有些定时配置可能要等到下一次更新才完全体现，刚启动时的第一个周期可能不符合预期。

### 6.16 `CR1.CEN` 是什么

`CR1` 是 Control Register 1，中文叫控制寄存器 1。`CEN` 是 Counter Enable，中文叫计数器使能。

它属于 TIM2 总控制层。

本课代码是：

```c
TIM2->CR1 |= TIM_CR1_CEN;
```

这一步之后 `CNT` 才开始按定时器时钟计数。前面的 `PSC`、`ARR`、`CCR1`、`OC1M`、`CC1E` 都只是把硬件摆好；`CEN` 才是启动时间轴。

如果漏掉 `CEN`，PA0 不会随时间翻转，因为 `CNT` 根本没有走到 `CCR1` 的机会。

### 6.17 `HAL_TIM_OC_Init()` 是什么

`HAL_TIM_OC_Init()` 是 HAL 的定时器输出比较初始化接口。

它属于 HAL 软件抽象层。它接收 `TIM_HandleTypeDef`，根据 `htim2.Instance` 和 `htim2.Init` 里的参数配置 TIM2 的基础时基和输出比较相关状态。

本课代码是：

```c
htim2.Instance = TIM2;
htim2.Init.Prescaler = 7200 - 1;
htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
htim2.Init.Period = 10000 - 1;
htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
HAL_TIM_OC_Init(&htim2);
```

这些字段不是“HAL 自己用着玩”的普通变量，它们最终会落到 TIM2 的寄存器配置上，尤其是 `PSC`、`ARR`、计数方向等。

如果 `HAL_TIM_OC_Init()` 没调用，后面的通道配置缺少定时器基础初始化，`HAL_TIM_OC_Start()` 也无法可靠地启动你想要的输出比较。

### 6.18 `TIM_OC_InitTypeDef` 是什么

`TIM_OC_InitTypeDef` 是 HAL 用来描述输出比较通道参数的结构体。

它属于 HAL 通道配置层。它不代表整个 TIM2，而是描述某一个输出比较通道怎么工作。

本课代码是：

```c
TIM_OC_InitTypeDef oc = {0};
oc.OCMode = TIM_OCMODE_TOGGLE;
oc.Pulse = 5000;
oc.OCPolarity = TIM_OCPOLARITY_HIGH;
```

`OCMode` 对应底层 `OC1M`，`Pulse` 对应底层 `CCR1`，`OCPolarity` 对应输出极性相关配置。

如果 `Pulse` 写错，就相当于 `CCR1` 写错；如果 `OCMode` 写错，就相当于 `OC1M` 写错。HAL 改了名字，但硬件含义没有变。

### 6.19 `HAL_TIM_OC_ConfigChannel()` 是什么

`HAL_TIM_OC_ConfigChannel()` 是 HAL 的通道配置接口。

它属于 HAL 到寄存器落地层。`HAL_TIM_OC_Init()` 主要配置 TIM2 的基础定时器部分；`HAL_TIM_OC_ConfigChannel()` 把 `TIM_OC_InitTypeDef` 里的输出比较参数写到指定通道。

本课代码是：

```c
HAL_TIM_OC_ConfigChannel(&htim2, &oc, TIM_CHANNEL_1);
```

这里的 `TIM_CHANNEL_1` 很重要。它告诉 HAL 把 `oc` 写入通道 1，也就是底层的 `CCR1`、`OC1M`、CH1 极性等相关位置。

如果通道参数选错，可能出现 TIM2 其他通道被配置了，但 PA0 对应的 TIM2_CH1 没有输出。

### 6.20 `HAL_TIM_OC_Start()` 是什么

`HAL_TIM_OC_Start()` 是 HAL 的输出比较启动接口。

它属于 HAL 运行控制层。配置寄存器只是把硬件设置好，Start 才会启用通道并启动定时器输出比较运行。

本课代码是：

```c
HAL_TIM_OC_Start(&htim2, TIM_CHANNEL_1);
```

它对应到底层，大致就是打开通道输出、使能计数器这类动作，也就是寄存器版里的 `CCER.CC1E` 和 `CR1.CEN` 所在的阶段。

如果只配置不 Start，常见现象就是代码没有报错，但 PA0 不出波形。

## 7. 寄存器版代码逐步讲解

### 7.1 `system_clock_72mhz_init()` 先把时间基准定下来

输出比较依赖准确时间。时间来自 TIM2 的计数，而 TIM2 的计数又来自系统时钟和总线时钟。

寄存器版先配置：

```c
FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;
RCC->CR |= RCC_CR_HSEON;
```

`FLASH_ACR_LATENCY_2` 是因为 72MHz 下 Flash 访问需要等待周期；`HSEON` 打开外部高速晶振。

随后等待：

```c
while ((RCC->CR & RCC_CR_HSERDY) == 0U) {
}
```

`HSERDY` 表示外部晶振稳定。晶振没稳定就继续配 PLL，后面的 72MHz 时间基准就不可靠。

### 7.2 PLL、AHB、APB 分频决定 TIM2 的计数来源

代码清理并设置 RCC 配置：

```c
RCC->CFGR &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2 |
               RCC_CFGR_PLLSRC | RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMULL |
               RCC_CFGR_SW);
RCC->CFGR |= RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 |
             RCC_CFGR_PPRE2_DIV1 | RCC_CFGR_PLLSRC | RCC_CFGR_PLLMULL9;
```

这里选择 HSE 作为 PLL 输入，9 倍频得到 72MHz。AHB 不分频，APB1 二分频，APB2 不分频。

TIM2 挂在 APB1 上。STM32F1 中，当 APB1 分频不为 1 时，定时器时钟会按规则得到更高的定时器输入时钟。本课程代码按 72MHz 来计算 TIM2 的计数源，所以 `PSC = 7200 - 1` 后得到 10kHz。

如果这里的时钟树和 `PSC` 计算不一致，PA0 的输出节奏一定会错。

### 7.3 `pc13_led_init()` 只是主循环心跳

PC13 初始化：

```c
RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;
GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
GPIOC->CRH |= GPIO_CRH_MODE13_1;
GPIOC->BSRR = GPIO_BSRR_BS13;
```

PC13 属于 GPIOC，所以要开 `IOPCEN`。PC13 是 13 号引脚，在 `GPIOC->CRH` 配置。`MODE13_1` 表示输出模式，`CNF13` 清零表示通用推挽。

这部分和 TIM2 输出比较没有直接关系。它只是给你一个“主循环还活着”的观察点。即使 PC13 正常闪，PA0 仍可能因为 TIM2 或复用配置错误而没有波形。

### 7.4 `pc13_toggle()` 用普通 GPIO 输出翻转

代码是：

```c
if ((GPIOC->ODR & GPIO_ODR_ODR13) != 0U) {
    GPIOC->BRR = GPIO_BRR_BR13;
} else {
    GPIOC->BSRR = GPIO_BSRR_BS13;
}
```

这里读 `ODR13` 判断当前输出状态，再用 `BRR` 拉低或用 `BSRR` 拉高。

这个函数故意和 PA0 形成对比：PC13 是 CPU 软件读写 GPIO 寄存器翻转；PA0 是 TIM2_CH1 在比较匹配时由硬件翻转。

### 7.5 开 GPIOA、AFIO、TIM2 时钟

输出比较初始化从 RCC 开始：

```c
RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN;
RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
```

`IOPAEN` 打开 GPIOA，因为 PA0 的模式寄存器属于 GPIOA。`AFIOEN` 打开复用功能模块。`TIM2EN` 打开 TIM2 外设本身。

如果 GPIOA 时钟没开，写 `GPIOA->CRL` 不会正确配置 PA0。若 TIM2 时钟没开，写 TIM2 寄存器不会得到正常计数输出。若后续涉及重映射而 AFIO 时钟没开，复用映射相关配置不会生效。

### 7.6 配置 PA0 为复用推挽输出

代码是：

```c
GPIOA->CRL &= ~(GPIO_CRL_MODE0 | GPIO_CRL_CNF0);
GPIOA->CRL |= GPIO_CRL_MODE0_1 | GPIO_CRL_CNF0_1;
```

PA0 是 0 号引脚，所以配置位置在 `CRL`。第一句清掉 PA0 的 `MODE0` 和 `CNF0`，第二句设置新模式。

`GPIO_CRL_MODE0_1` 让 `MODE0 = 10`，表示输出速度 2MHz。`GPIO_CRL_CNF0_1` 让 `CNF0 = 10`，表示复用推挽输出。

硬件后果是：PA0 的输出驱动路径接到 TIM2_CH1，而不是只听普通 GPIO 输出寄存器。本课想观察的是定时器通道输出，所以这一步不能省。

### 7.7 设置 `PSC` 得到 10kHz 计数频率

代码是：

```c
TIM2->PSC = 7200U - 1U;
```

定时器预分频实际除数是写入值加 1。这里写 7199，实际除以 7200。

按 72MHz 定时器时钟计算：

```text
72MHz / 7200 = 10kHz
```

所以 `CNT` 每 0.1ms 加 1。后面所有 `ARR`、`CCR1` 的时间意义，都建立在这个 10kHz 上。

### 7.8 设置 `ARR` 得到 1 秒计数周期

代码是：

```c
TIM2->ARR = 10000U - 1U;
```

这里写 9999，表示 `CNT` 从 0 数到 9999，一共 10000 个计数。

`CNT` 是 10kHz，10000 个计数就是 1 秒。也就是说 TIM2 的计数周期是 1 秒。

如果你把 `ARR` 改成 5000 - 1，计数周期会变成 0.5 秒；如果 `CCR1` 仍然是 5000，就会出现比较值刚好超出可达范围或边界不符合预期的问题。

### 7.9 设置 `CCR1` 得到通道 1 比较点

代码是：

```c
TIM2->CCR1 = 5000U;
```

`CCR1` 属于通道 1。输出比较时，它表示“当 `CNT` 等于多少时触发通道 1 比较事件”。

在本课里，`CNT` 每 0.1ms 加 1，5000 个计数是 0.5 秒。所以在每个 1 秒计数周期中，TIM2_CH1 会在中间位置产生一次比较匹配。

这并不等于“每 0.5 秒匹配一次”。因为 `CCR1` 固定为 5000，`CNT` 每轮只会经过一次 5000。匹配之后要等下一轮计数再次走到 5000。

### 7.10 清理 `OC1M` 旧模式

代码是：

```c
TIM2->CCMR1 &= ~TIM_CCMR1_OC1M;
```

`OC1M` 是多 bit 字段。多 bit 字段不能只靠 `|=` 随便叠加，因为旧值可能还在。

这句的硬件意义是：先把通道 1 的输出比较模式字段清成 000，给下一句设置 toggle 模式留出干净位置。

如果不清，假设旧值中某个 bit 已经是 1，你再 `|=` 新值，最后可能得到另一个模式编码。

### 7.11 设置 `OC1M = 011` 为 toggle

代码是：

```c
TIM2->CCMR1 |= TIM_CCMR1_OC1M_0 | TIM_CCMR1_OC1M_1;
```

`OC1M_0` 和 `OC1M_1` 置 1，`OC1M_2` 保持 0，所以 `OC1M = 011`。

在输出比较模式里，`011` 表示 toggle on match。硬件后果是：当 `CNT == CCR1` 时，TIM2_CH1 输出状态翻转。

如果这里不是 toggle，PA0 的表现会完全不同。比如 frozen 模式不会改输出，active 模式会在匹配时变为有效电平，PWM 模式则会按照周期内高低电平规则输出。

### 7.12 设置 `CC1E` 打开通道 1 输出

代码是：

```c
TIM2->CCER |= TIM_CCER_CC1E;
```

`CCMR1` 决定“匹配时做什么”，`CCER` 决定“通道输出是否打开”。这两个不是一回事。

本句设置 `CC1E = 1`，允许 TIM2_CH1 输出。PA0 已经配置成复用推挽输出，通道也打开后，TIM2_CH1 的状态变化才能体现在 PA0 引脚上。

如果漏掉这句，最典型现象是内部配置看起来都对，但 PA0 没波形。

### 7.13 产生一次更新事件

代码是：

```c
TIM2->EGR = TIM_EGR_UG;
```

这句通过软件产生更新事件，让预分频器等定时器内部配置装载。它常放在启动计数器之前，用来让刚写入的配置进入一致状态。

硬件后果是 TIM2 的时基逻辑以新配置准备运行，而不是等自然更新后才完全进入新状态。

### 7.14 启动 TIM2 计数器

代码是：

```c
TIM2->CR1 |= TIM_CR1_CEN;
```

`CEN` 是计数器使能。置位后，`CNT` 开始按 `PSC` 分频后的节拍向上计数。

至此，输出比较链路完整闭合：TIM2 有时钟，PA0 是复用输出，时基已经设置，比较点已经设置，匹配动作是 toggle，通道输出已打开，计数器开始运行。

### 7.15 主循环不参与 PA0 边沿

主循环代码是：

```c
while (1) {
    pc13_toggle();
    delay_cycles(3600000U);
}
```

这里没有任何 `GPIOA->BSRR`、`GPIOA->BRR` 或 `TIM2->CCR1` 写操作。PA0 的翻转不靠主循环。

这正是本课要证明的点：只要定时器输出比较配置正确，硬件会在后台按比较事件驱动引脚。

## 8. HAL 版代码逐步讲解

### 8.1 `HAL_Init()` 建立 HAL 基础环境

HAL 版主函数先调用：

```c
HAL_Init();
```

它属于 HAL 运行基础层，会初始化 HAL 库需要的基础状态，通常也会配置 SysTick 作为 HAL 延时节拍。

这和 PA0 输出比较不是同一条硬件链路，但 `HAL_Delay(500)` 需要 HAL 的时间基准，所以它必须出现在主函数开头。

### 8.2 HAL 版系统时钟和寄存器版是同一目标

HAL 版用：

```c
RCC_OscInitTypeDef osc = {0};
RCC_ClkInitTypeDef clk = {0};
```

`osc` 描述振荡器和 PLL，`clk` 描述 SYSCLK、HCLK、PCLK1、PCLK2 的选择和分频。

这些结构体最终对应寄存器版中的 `RCC->CR`、`RCC->CFGR`、`FLASH->ACR` 等配置。HAL 只是把“写哪个 bit”的过程包装成字段。

### 8.3 `__HAL_RCC_GPIOA_CLK_ENABLE()` 对应 GPIOA 时钟

HAL 版输出比较初始化先写：

```c
__HAL_RCC_GPIOA_CLK_ENABLE();
__HAL_RCC_TIM2_CLK_ENABLE();
```

`__HAL_RCC_GPIOA_CLK_ENABLE()` 对应寄存器版 `RCC_APB2ENR_IOPAEN`。它让 GPIOA 的配置寄存器可用。

`__HAL_RCC_TIM2_CLK_ENABLE()` 对应寄存器版 `RCC_APB1ENR_TIM2EN`。它让 TIM2 的计数和通道寄存器可用。

如果只开 GPIOA 不打开 TIM2，PA0 模式能配，但没有定时器输出。若只开 TIM2 不打开 GPIOA，通道内部可能配置了，但 PA0 引脚模式不对。

### 8.4 `GPIO_InitTypeDef` 描述 PA0 模式

HAL 版写：

```c
GPIO_InitTypeDef gpio = {0};
gpio.Pin = GPIO_PIN_0;
gpio.Mode = GPIO_MODE_AF_PP;
gpio.Speed = GPIO_SPEED_FREQ_LOW;
HAL_GPIO_Init(GPIOA, &gpio);
```

`gpio.Pin = GPIO_PIN_0` 表示配置 PA0。`GPIOA` 作为第一个参数，说明这个 0 号引脚属于 A 口。

`gpio.Mode = GPIO_MODE_AF_PP` 对应寄存器版 `CNF0 = 10` 的复用推挽输出。`gpio.Speed = GPIO_SPEED_FREQ_LOW` 对应 F1 GPIO 输出速度选择，本课不需要高速边沿，低速足够。

`HAL_GPIO_Init()` 会根据这些字段写 `GPIOA->CRL` 中 PA0 对应的 4 个配置 bit。

### 8.5 `TIM_HandleTypeDef htim2` 表示 TIM2 这个外设实例

HAL 版有全局对象：

```c
static TIM_HandleTypeDef htim2;
```

`TIM_HandleTypeDef` 是 HAL 对一个定时器外设的描述。它里面既有 `Instance` 指向哪个硬件定时器，也有 `Init` 保存基础配置。

本课写：

```c
htim2.Instance = TIM2;
```

这句话对应寄存器版所有 `TIM2->...` 的目标选择。没有它，HAL 不知道后续配置应该落到 TIM2、TIM3 还是其他定时器。

### 8.6 `Prescaler` 对应 `PSC`

HAL 版写：

```c
htim2.Init.Prescaler = 7200 - 1;
```

这对应寄存器版：

```c
TIM2->PSC = 7200U - 1U;
```

两者硬件意义完全相同：把 TIM2 计数源除以 7200，让 `CNT` 以 10kHz 频率计数。

如果 HAL 版和寄存器版这里不一致，两个工程下载后 PA0 翻转节奏就会不同。

### 8.7 `Period` 对应 `ARR`

HAL 版写：

```c
htim2.Init.Period = 10000 - 1;
```

这对应寄存器版：

```c
TIM2->ARR = 10000U - 1U;
```

`Period` 这个名字更偏 HAL 语义，底层就是自动重装载值。它决定 `CNT` 一轮计数多长。

本课 `Period = 9999`，在 10kHz 计数频率下得到 1 秒计数周期。

### 8.8 `CounterMode` 对应向上计数

HAL 版写：

```c
htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
```

它表示 `CNT` 从 0 往 `ARR` 数。输出比较中的 `CCR1 = 5000` 是建立在向上计数理解上的：计数先经过 0，再走到 5000，再走到 9999。

如果改成其他计数模式，比较事件发生的时间点和周期理解会改变。基础课程里先用向上计数，把时间轴讲清楚。

### 8.9 `HAL_TIM_OC_Init()` 写入 TIM2 基础配置

代码是：

```c
HAL_TIM_OC_Init(&htim2);
```

它读取 `htim2.Instance` 和 `htim2.Init`，完成输出比较所需的定时器基础初始化。

从底层看，它对应寄存器版设置 `PSC`、`ARR`、计数方向等时基相关操作。HAL 调用成功后，TIM2 已经知道自己的计数频率和计数周期。

如果它返回错误而代码没有处理，后续即使继续调用通道配置，也不代表硬件已经处在正确状态。

### 8.10 `OCMode` 对应 `OC1M`

HAL 版写：

```c
oc.OCMode = TIM_OCMODE_TOGGLE;
```

这对应寄存器版：

```c
TIM2->CCMR1 |= TIM_CCMR1_OC1M_0 | TIM_CCMR1_OC1M_1;
```

`TIM_OCMODE_TOGGLE` 不是一个抽象概念，它最终会让通道 1 的输出比较模式字段成为 toggle 编码。硬件后果仍然是 `CNT == CCR1` 时翻转通道输出。

### 8.11 `Pulse` 对应 `CCR1`

HAL 版写：

```c
oc.Pulse = 5000;
```

这对应寄存器版：

```c
TIM2->CCR1 = 5000U;
```

`Pulse` 这个名字在 PWM 课里更像“脉宽”，在输出比较里你可以先把它理解成比较值。它决定通道 1 在 `CNT` 等于多少时发生比较匹配。

如果你要改变 PA0 在一轮计数中的翻转位置，HAL 版改 `Pulse`，寄存器版改 `CCR1`。

### 8.12 `OCPolarity` 对应输出极性

HAL 版写：

```c
oc.OCPolarity = TIM_OCPOLARITY_HIGH;
```

它对应通道输出极性相关配置。对于本课的 toggle 模式，重点不是占空比高低有效，而是通道输出状态如何通过极性映射到引脚电平。

保持 `TIM_OCPOLARITY_HIGH` 可以让通道输出按常规极性出现在 PA0。若改成低极性，外部观察到的高低关系可能反相。

### 8.13 `HAL_TIM_OC_ConfigChannel()` 把通道参数落到 CH1

代码是：

```c
HAL_TIM_OC_ConfigChannel(&htim2, &oc, TIM_CHANNEL_1);
```

这一步把 `oc` 里的 `OCMode`、`Pulse`、`OCPolarity` 写到 TIM2 的通道 1 相关寄存器。

`TIM_CHANNEL_1` 对应 TIM2_CH1，也对应 PA0 的默认复用输出。如果这里换成 `TIM_CHANNEL_2`，HAL 会配置通道 2，而不是本课接到 PA0 的通道 1。

### 8.14 `HAL_TIM_OC_Start()` 同时打开运行链路

代码是：

```c
HAL_TIM_OC_Start(&htim2, TIM_CHANNEL_1);
```

这一步对应寄存器版的后半段启动动作：启用通道输出，并启动定时器计数。

你可以把它理解成 HAL 版的“让刚才配置好的输出比较真正跑起来”。只配置 `OCMode` 和 `Pulse` 不调用 Start，硬件不会按你的预期在 PA0 上输出翻转波形。

### 8.15 HAL 版 PC13 心跳和 PA0 输出比较互相独立

主循环是：

```c
while (1) {
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    HAL_Delay(500);
}
```

`HAL_GPIO_TogglePin()` 对应普通 GPIO 翻转，`HAL_Delay()` 依赖 HAL 时间基准。它们只影响 PC13。

PA0 没有在主循环里被写。PA0 的输出比较在 `HAL_TIM_OC_Start()` 后由 TIM2 硬件持续执行。

## 9. 两个版本真正应该怎么学

寄存器版要盯住四个层次。

第一是时钟：GPIOA、AFIO、TIM2 的时钟有没有打开。没有时钟，后面的寄存器写入不是完整有效的硬件配置。

第二是引脚：PA0 是否是复用推挽输出。定时器通道要出到外部脚，GPIO 模式必须给外设让路。

第三是时基：`PSC` 和 `ARR` 决定 `CNT` 怎么跑。没有稳定时间轴，`CCR1` 就没有时间意义。

第四是通道：`CCR1` 决定匹配点，`OC1M` 决定匹配动作，`CC1E` 决定输出是否打开。

HAL 版要把字段翻译回硬件。

`htim2.Instance = TIM2` 选择定时器。`Prescaler` 对应 `PSC`。`Period` 对应 `ARR`。`oc.Pulse` 对应 `CCR1`。`oc.OCMode = TIM_OCMODE_TOGGLE` 对应 `OC1M = 011`。`HAL_TIM_OC_Start()` 对应通道输出和计数器启动。

只要你能完成这些翻译，HAL 就不是黑箱。

## 10. 检验问题清单

### 10.1 PA0 为什么必须配置成复用推挽输出？

因为 PA0 本课要输出 TIM2_CH1 信号，而不是普通 GPIO 输出值。复用功能让引脚输出来源切换到片上外设，推挽输出让引脚能主动拉高和拉低。

### 10.2 `CCR1 = 5000` 表示每 0.5 秒翻转一次吗？

不准确。它表示在每个计数周期中，当 `CNT` 走到 5000 这个点时翻转一次。本课 `ARR = 9999`，一轮是 1 秒，所以每轮只翻转一次；完整高低周期约 2 秒。

### 10.3 只设置 `OC1M`，不设置 `CC1E`，PA0 会有波形吗？

通常不会有预期波形。`OC1M` 只决定匹配动作，`CC1E` 才打开通道 1 输出。内部事件和外部引脚输出不是同一个开关。

### 10.4 `PSC` 和 `ARR` 谁决定比较点？

`PSC` 和 `ARR` 决定时间轴和计数周期，`CCR1` 决定比较点。`PSC` 影响每个计数的时间长度，`ARR` 影响一轮多长，`CCR1` 影响这一轮中的哪个位置触发 CH1。

### 10.5 HAL 里的 `Pulse` 对应哪个寄存器？

本课 `oc.Pulse = 5000` 对应 `TIM2->CCR1 = 5000U`。通道是 `TIM_CHANNEL_1`，所以落到 `CCR1`。

### 10.6 HAL 里的 `TIM_OCMODE_TOGGLE` 对应哪个寄存器字段？

它对应 `CCMR1` 里的 `OC1M` 字段。对 TIM2_CH1 来说，就是把输出比较 1 模式设置成 toggle。

### 10.7 为什么本课不需要 TIM2 中断？

因为 PA0 的翻转由 TIM2_CH1 输出比较硬件完成。CPU 不需要在每次匹配时进入中断函数。中断可以用于通知软件，但不是本课生成 PA0 边沿的必要条件。

### 10.8 PC13 正常闪烁，能证明 PA0 一定正常吗？

不能。PC13 只证明主循环、GPIOC 和延时大体正常。PA0 还依赖 GPIOA 复用模式、TIM2 时钟、输出比较模式、通道输出使能等配置。

## 11. 工程实现步骤

### 11.1 需求分析

本课需求不是“让 LED 闪”，而是“让定时器通道在比较匹配时自动改变引脚输出”。

所以工程目标要拆成：

- TIM2 能按 72MHz 派生出的时钟稳定计数。
- TIM2_CH1 有一个明确比较点。
- 比较匹配时执行 toggle。
- TIM2_CH1 输出能通过 PA0 到达外部。
- 主循环可以继续做其他事，不参与 PA0 每个边沿。

### 11.2 硬件核查

先确认板子和接线：

- 使用 STM32F103C8T6。
- PA0 是 TIM2_CH1 默认引脚。
- PA0 外接 LED 时必须串限流电阻。
- 用示波器或逻辑分析仪时，地线必须和开发板共地。
- PC13 板载 LED 只是心跳，不要把它当成 TIM2_CH1 输出。

如果你观察不到 PA0，先确认自己测的是 PA0，不是 PC13，也不是别的 TIM2 通道引脚。

### 11.3 寄存器路线

寄存器版实现顺序应该是：

1. 配置系统时钟到 72MHz。
2. 打开 GPIOA、AFIO、TIM2 时钟。
3. 配置 PA0 为复用推挽输出。
4. 写 `TIM2->PSC = 7200 - 1`。
5. 写 `TIM2->ARR = 10000 - 1`。
6. 写 `TIM2->CCR1 = 5000`。
7. 清 `CCMR1.OC1M` 旧值。
8. 设置 `OC1M = 011` toggle。
9. 设置 `CCER.CC1E`。
10. 写 `EGR.UG`。
11. 设置 `CR1.CEN`。

调试时不要一上来只盯 `CCR1`。PA0 没波形时，通道输出和 GPIO 复用更常见。

### 11.4 HAL 路线

HAL 版实现顺序应该是：

1. `HAL_Init()`。
2. 配置系统时钟。
3. `__HAL_RCC_GPIOA_CLK_ENABLE()` 和 `__HAL_RCC_TIM2_CLK_ENABLE()`。
4. 用 `GPIO_InitTypeDef` 把 PA0 配成 `GPIO_MODE_AF_PP`。
5. `htim2.Instance = TIM2`。
6. 在 `htim2.Init` 里设置 `Prescaler`、`Period`、`CounterMode`。
7. 调用 `HAL_TIM_OC_Init(&htim2)`。
8. 用 `TIM_OC_InitTypeDef` 设置 `OCMode`、`Pulse`、`OCPolarity`。
9. 调用 `HAL_TIM_OC_ConfigChannel(&htim2, &oc, TIM_CHANNEL_1)`。
10. 调用 `HAL_TIM_OC_Start(&htim2, TIM_CHANNEL_1)`。

HAL 路线最容易漏的是 Start。配置结构体只是描述意图，Start 才让输出比较开始运行。

### 11.5 工程思维

输出比较的工程价值在于减少 CPU 参与。CPU 不必为了每个边沿准时醒来，也不必在主循环里用延时凑时间。

这类外设使用方式的思维是：先把硬件链路配置完整，然后让外设自己按规则运行。主循环可以继续处理其他任务。

本课也是 PWM 的前置课。你现在看到的是“比较匹配时翻转”，下一课会看到“比较值决定周期内高电平持续多久”。它们都建立在 `CNT`、`ARR`、`CCR`、通道输出模式这套结构上。

### 11.6 常见工程陷阱

第一个陷阱是把 PA0 当普通 GPIO。只要你还在想用 `GPIOA->BSRR` 周期性写 PA0，就没有抓住输出比较的核心。

第二个陷阱是忘记 `CC1E`。模式配置和输出使能是两道门，少一道都不行。

第三个陷阱是把 `CCR1` 理解成周期。`CCR1` 是一轮计数中的比较点，周期主要由 `ARR` 和计数频率决定。

第四个陷阱是 HAL 配置了通道但没有调用 `HAL_TIM_OC_Start()`。

第五个陷阱是通道和引脚不匹配。PA0 默认是 TIM2_CH1，不是 TIM2 任意通道。

## 12. 运行现象

下载寄存器版或 HAL 版后，PC13 会周期性闪烁，说明主循环在运行。

PA0 会由 TIM2_CH1 输出比较自动翻转。按本课参数：

```text
TIM2 计数频率 = 72MHz / 7200 = 10kHz
ARR = 9999，一轮计数时间 = 1s
CCR1 = 5000，每轮 0.5s 位置比较一次
toggle 模式，每次比较匹配翻转一次
```

所以 PA0 大约每 1 秒翻转一次电平，完整高低周期约 2 秒。

如果用示波器看，你应该能看到 PA0 周期性高低变化。如果用外接 LED 看，LED 会较慢地亮灭变化。PC13 的闪烁频率由主循环延时决定，不代表 PA0 的精确波形。

## 13. 常见问题排查

### 13.1 PA0 完全没有波形

先查三件事：

- GPIOA 时钟是否打开。
- PA0 是否配置为复用推挽输出。
- TIM2_CH1 是否通过 `CC1E` 打开输出。

如果 PC13 正常闪，说明程序没有死，但不能证明 TIM2_CH1 输出链路正确。

### 13.2 PA0 一直高或一直低

重点检查 `OC1M` 是否真的是 toggle。寄存器版确认 `CCMR1.OC1M = 011`，HAL 版确认 `oc.OCMode = TIM_OCMODE_TOGGLE`。

还要检查 `CCR1` 是否小于等于 `ARR` 的有效计数范围。如果 `CCR1` 超出 `CNT` 能到达的范围，就不会发生比较匹配。

### 13.3 波形频率不符合预期

重新计算三项：

```text
TIM2 计数频率 = TIM2 时钟 / (PSC + 1)
计数周期 = (ARR + 1) / TIM2 计数频率
翻转发生点 = CCR1 / TIM2 计数频率
```

本课要特别注意 APB1 定时器时钟和系统时钟的关系。代码按 TIM2 计数源 72MHz 来设置参数。

### 13.4 HAL 版没有输出

按顺序查：

- 是否调用 `__HAL_RCC_TIM2_CLK_ENABLE()`。
- PA0 是否 `GPIO_MODE_AF_PP`。
- `HAL_TIM_OC_Init()` 是否执行。
- `HAL_TIM_OC_ConfigChannel()` 的通道是否是 `TIM_CHANNEL_1`。
- 是否调用 `HAL_TIM_OC_Start()`。

HAL 版最常见的问题是只配置通道，不启动通道。

### 13.5 修改 `CCR1` 后现象和想的不一样

先确认你改的是比较点，不是周期。`CCR1` 改变的是一轮计数中翻转发生的位置；`ARR` 和 `PSC` 才主要决定一轮多久。

如果 `CCR1` 固定不变，toggle 每轮只发生一次。想做“每隔固定时间连续翻转”的调度，可以在中断或回调里动态更新下一次比较值，但那不是本课代码当前做的事。

### 13.6 PC13 闪烁正常但 PA0 不动

这通常说明主循环、GPIOC、延时没问题，但 TIM2 输出比较链路有问题。

优先看 PA0 复用推挽、TIM2 时钟、`CCMR1.OC1M`、`CCER.CC1E`、`CR1.CEN`。不要被 PC13 正常闪烁带偏。

## 14. 本课最核心的结论

输出比较不是 CPU 延时翻转 GPIO，而是定时器硬件在 `CNT == CCR1` 时按通道模式改变输出。

本课中：

- `PSC` 决定 `CNT` 的计数速度。
- `ARR` 决定一轮计数周期。
- `CCR1` 决定通道 1 的比较点。
- `OC1M` 决定匹配时执行 toggle。
- `CC1E` 决定 TIM2_CH1 输出是否打开。
- PA0 的复用推挽模式决定通道信号能否到达引脚。

把这六个点连起来，你就能从“看到 PA0 翻转”一直追到“哪个寄存器 bit 让它翻转”。

## 15. 建议你现在怎么读这节课

先不要背 HAL API。先抓住硬件句子：

```text
TIM2 的 CNT 在跑，跑到 CCR1 时，CH1 根据 OC1M 翻转输出，CC1E 允许输出到 PA0。
```

然后回到寄存器版代码里按顺序找：

- 谁让 TIM2 有时钟。
- 谁让 PA0 接到 TIM2_CH1。
- 谁让 `CNT` 以 10kHz 跑。
- 谁规定一轮是 1 秒。
- 谁规定 0.5 秒位置比较。
- 谁规定比较时翻转。
- 谁打开通道输出。

最后再看 HAL 版，把 `Prescaler`、`Period`、`Pulse`、`OCMode`、`Start` 翻译回对应寄存器。

## 16. 扩展练习

1. 把 `CCR1` 改成 2500，观察 PA0 在每轮更早的位置翻转。
2. 把 `CCR1` 改成 7500，观察 PA0 在每轮更晚的位置翻转。
3. 把 `ARR` 改成 `5000 - 1`，重新计算 PA0 翻转间隔。
4. 注释掉 `TIM2->CCER |= TIM_CCER_CC1E;`，观察内部配置完整但外部无输出的故障。
5. HAL 版注释掉 `HAL_TIM_OC_Start()`，观察只配置不启动的现象。
6. 尝试把 `TIM_OCMODE_TOGGLE` 改成其他输出比较模式，对比 PA0 波形变化。
7. 思考下一课 PWM：如果不再是匹配时翻转，而是在周期内按 `CCR1` 控制高低电平，占空比会怎样形成。

## 17. 下一课预告

上一课：[06_timer_base](../06_timer_base/README.md)

下一课：[08_pwm_basic](../08_pwm_basic/README.md)

下一课会继续使用 TIM2_CH1 和 PA0，但输出模式从“比较匹配翻转”变成 PWM。到那时，`ARR` 会决定 PWM 周期，`CCR1` 会决定占空比，PA0 上看到的就不再只是每次匹配翻转，而是周期性高低电平脉冲。

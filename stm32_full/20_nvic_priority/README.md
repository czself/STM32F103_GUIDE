# 第 20 课：NVIC 优先级

## 1. 本课到底在学什么

这节课表面上是在做一个现象：

- `TIM2` 周期性让 LED 长亮一小段时间
- `PA0` 按键触发外部中断
- 如果在 LED 长亮期间按下按键，LED 会立刻插入一次短暂的反向脉冲

但这节课真正要学的不是“让 LED 闪出花样”，而是 STM32 中断系统里非常关键的一层：

- 外设可以产生中断请求
- 但 CPU 是否响应这个请求，要经过 `NVIC`
- 多个中断同时出现时，`NVIC` 要根据优先级决定先响应谁
- 高优先级中断可以打断正在执行的低优先级中断，这叫“抢占”

前面你已经学过 `EXTI` 和 `TIM2` 中断，但那时候重点是“怎么进入中断”。本课开始关注更深一层：

- 如果两个中断都来了，谁先执行？
- 一个中断正在执行时，另一个中断能不能插进来？
- `NVIC_SetPriority()` 这类函数到底是在控制什么？

这就是本课的核心。

## 2. 本课学习目标

学完本课，你应该能回答清楚：

- `NVIC` 是什么，它为什么不属于某一个外设
- `TIM2_IRQn` 和 `EXTI0_IRQn` 这类名字为什么会出现
- 外设自己的中断使能和 `NVIC_EnableIRQ()` 有什么区别
- 为什么 `EXTI0` 优先级数字写 `0`，`TIM2` 写 `2`，反而是 `EXTI0` 更高
- 什么叫“抢占”
- 为什么本课故意在 `TIM2_IRQHandler()` 里停留一段时间
- `HAL_NVIC_SetPriority()`、`HAL_TIM_Base_Start_IT()`、`HAL_GPIO_EXTI_IRQHandler()` 分别封装了哪些底层动作

## 3. 本课目录结构

```text
20_nvic_priority/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

说明：

- `reg/`：寄存器版，用来直接看清 TIM2、EXTI、NVIC 三者如何配合
- `hal/`：HAL版，用来理解工程里常见 API 和底层寄存器链路的对应关系

## 4. 实验硬件

### 4.1 开发板

- STM32F103C8T6 BluePill

### 4.2 板载 LED

- `PC13`

多数 BluePill 板载 LED 是低电平点亮：

- `PC13 = 0`：LED 亮
- `PC13 = 1`：LED 灭

### 4.3 按键

- `PA0` 外接按键
- 按键另一端接 `GND`
- 程序内部打开 PA0 上拉

所以按键未按下时：

- PA0 被内部上拉为高电平

按键按下时：

- PA0 被接到 GND，变成低电平

因此本课选择：

- 下降沿触发 EXTI0 中断

## 5. 先建立完整脑图

这一课有两条中断链路。

### 5.1 TIM2 低优先级中断链路

```text
TIM2 计数器运行
  -> 计数到 ARR
  -> 产生更新事件
  -> TIM2->SR.UIF 置 1
  -> 如果 TIM2->DIER.UIE = 1，就向 NVIC 发出 TIM2 中断请求
  -> 如果 NVIC 里 TIM2_IRQn 已使能，CPU 进入 TIM2_IRQHandler
  -> TIM2_IRQHandler 里让 LED 长亮一小段时间
```

### 5.2 PA0 高优先级中断链路

```text
按下 PA0
  -> PA0 从高电平变成低电平
  -> EXTI0 检测到下降沿
  -> EXTI->PR.PR0 置 1
  -> 如果 EXTI->IMR.MR0 = 1，就向 NVIC 发出 EXTI0 中断请求
  -> 如果 NVIC 里 EXTI0_IRQn 已使能，CPU 进入 EXTI0_IRQHandler
  -> EXTI0_IRQHandler 里让 LED 做一次反向短脉冲
```

### 5.3 本课要观察的抢占链路

最关键的是这个时刻：

```text
CPU 正在 TIM2_IRQHandler 里执行
  -> LED 正处于长亮窗口
  -> 此时按下 PA0
  -> EXTI0 中断请求到达 NVIC
  -> NVIC 发现 EXTI0 优先级高于 TIM2
  -> CPU 暂停 TIM2_IRQHandler
  -> 先执行 EXTI0_IRQHandler
  -> LED 立刻出现一次短暂反向脉冲
  -> EXTI0_IRQHandler 结束
  -> CPU 回到 TIM2_IRQHandler 继续执行
```

所以你看到的“长亮中突然短暂变化”，不是普通闪灯效果，而是抢占发生的证据。

## 6. 核心名词解释

### 6.1 `NVIC` 是什么

`NVIC` 全称是：

- Nested Vectored Interrupt Controller

中文通常叫：

- 嵌套向量中断控制器

它属于 Cortex-M3 内核，不属于 GPIO、TIM、USART 这种 STM32 外设。

它负责三件事：

1. 接收各个外设送来的中断请求
2. 判断这个中断在 NVIC 里有没有被使能
3. 根据优先级决定 CPU 应该先响应哪个中断

所以外设中断不是“外设直接调用 C 函数”。更准确的链路是：

```text
外设产生中断请求 -> NVIC 仲裁 -> CPU 跳转到中断处理函数
```

### 6.2 `IRQn` 是什么

`IRQn` 可以理解成“中断编号”。

例如：

- `EXTI0_IRQn`：EXTI0 对应的中断编号
- `TIM2_IRQn`：TIM2 对应的中断编号

为什么要有编号？

因为 NVIC 面对的是很多中断源，它必须用编号区分：

- 当前要使能哪个中断
- 当前要设置哪个中断的优先级
- 当前进入哪个中断向量入口

所以你看到：

```c
NVIC_SetPriority(EXTI0_IRQn, 0U);
NVIC_EnableIRQ(EXTI0_IRQn);
```

意思是：

- 设置 EXTI0 这个中断编号的优先级
- 允许 NVIC 响应 EXTI0 这个中断编号

### 6.3 外设中断使能和 NVIC 使能有什么区别

这是本课非常重要的点。

以 TIM2 为例，完整中断链路至少需要两道门：

第一道门在外设内部：

```c
TIM2->DIER |= TIM_DIER_UIE;
```

它的意思是：

- TIM2 发生更新事件时，允许 TIM2 向外发出中断请求

第二道门在 NVIC：

```c
NVIC_EnableIRQ(TIM2_IRQn);
```

它的意思是：

- NVIC 允许接收并响应 TIM2 这个中断请求

如果只开第一道门，不开第二道门：

- TIM2 可以发请求
- 但 NVIC 不响应
- CPU 不会进入 `TIM2_IRQHandler`

如果只开第二道门，不开第一道门：

- NVIC 愿意响应 TIM2
- 但 TIM2 根本不发请求
- CPU 也不会进入 `TIM2_IRQHandler`

所以两者缺一不可。

### 6.4 优先级数字为什么越小越高

在 Cortex-M 系列里，常见规则是：

- 优先级数字越小，优先级越高

本课使用：

```c
NVIC_SetPriority(EXTI0_IRQn, 0U);
NVIC_SetPriority(TIM2_IRQn, 2U);
```

所以：

- `EXTI0` 优先级高
- `TIM2` 优先级低

当 `TIM2_IRQHandler()` 正在执行时，如果 `EXTI0` 来了，`EXTI0` 可以抢占 `TIM2`。

### 6.5 抢占是什么

抢占不是简单的“谁先来谁先执行”。

抢占指的是：

- CPU 正在执行一个低优先级中断
- 此时来了一个更高优先级中断
- CPU 暂停低优先级中断
- 先执行高优先级中断
- 高优先级中断返回后，再继续执行低优先级中断

这就是本课 Demo 要让你看到的事情。

### 6.6 优先级分组先讲到什么程度

STM32 里还存在“抢占优先级”和“响应优先级/子优先级”的分组问题。

但本课先不展开复杂分组，只抓住最小可运行理解：

- 本课只需要比较谁能抢占谁
- 所以只关心抢占优先级
- HAL 工程默认使用的优先级分组足够支持本课实验

更复杂的优先级分组，可以在后面多中断系统设计时再展开。

## 7. 寄存器版代码讲解

寄存器版代码在：

- [reg/src/main.c](/home/myself/workspace/mcu/stm32/STM32F103C8T6/STM32F103C8T6_GUIDE/20_nvic_priority/reg/src/main.c)

### 7.1 配置 PC13 LED

```c
RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;
GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
GPIOC->CRH |= GPIO_CRH_MODE13_1;
```

这里和第一课一样：

- 先打开 GPIOC 时钟
- 再把 PC13 配成推挽输出
- 后面才可以用 `BSRR/BRR` 控制 LED

这部分不是本课重点，但它提供可观察现象。

### 7.2 配置 PA0 为 EXTI0 输入

```c
RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN;
```

这里为什么要开两个时钟？

- `GPIOA`：因为 PA0 是 GPIOA 的引脚
- `AFIO`：因为 EXTI0 要选择自己来自 PA0、PB0 还是 PC0，这个映射由 AFIO 管

```c
AFIO->EXTICR[0] &= ~AFIO_EXTICR1_EXTI0;
```

`EXTICR[0]` 负责 EXTI0 到 EXTI3 的端口选择。

对 EXTI0 来说：

- 写 `0000`：选择 PA0
- 写 `0001`：选择 PB0
- 写 `0010`：选择 PC0

本课用 PA0，所以清零。

```c
EXTI->IMR |= EXTI_IMR_MR0;
EXTI->FTSR |= EXTI_FTSR_TR0;
EXTI->RTSR &= ~EXTI_RTSR_TR0;
```

这三句分别表示：

- 允许 EXTI0 产生中断请求
- 选择下降沿触发
- 不使用上升沿触发

### 7.3 配置 TIM2 更新中断

```c
TIM2->PSC = 7200U - 1U;
TIM2->ARR = 20000U - 1U;
```

当前时钟下：

- TIM2 计数时钟约 72MHz
- `PSC=7199` 后变成 10kHz
- `ARR=19999` 后约 2 秒更新一次

```c
TIM2->DIER |= TIM_DIER_UIE;
```

`DIER` 是 DMA/Interrupt Enable Register。

`UIE` 是 Update Interrupt Enable。

它的作用是：

- TIM2 更新事件发生时，允许 TIM2 发出中断请求

### 7.4 设置 NVIC 优先级

```c
NVIC_SetPriority(EXTI0_IRQn, 0U);
NVIC_EnableIRQ(EXTI0_IRQn);

NVIC_SetPriority(TIM2_IRQn, 2U);
NVIC_EnableIRQ(TIM2_IRQn);
```

这就是本课的核心配置。

这里不是在配置 EXTI 或 TIM2 外设本身，而是在配置 Cortex-M3 内核里的 NVIC。

最终关系是：

```text
EXTI0 优先级 0 -> 高
TIM2  优先级 2 -> 低
```

所以 EXTI0 可以抢占 TIM2。

### 7.5 为什么中断里要清标志

TIM2 中断里：

```c
TIM2->SR &= ~TIM_SR_UIF;
```

EXTI0 中断里：

```c
EXTI->PR = EXTI_PR_PR0;
```

这两句都是清中断标志。

如果不清标志，会出现：

- 中断函数退出
- 标志仍然存在
- CPU 立刻再次进入同一个中断

注意 EXTI 的 `PR` 是写 1 清除，不是写 0 清除。

### 7.6 为什么本课故意在中断里忙等待

正常项目中不建议在中断里长时间等待。

但本课为了观察抢占，需要让 `TIM2_IRQHandler()` 执行时间足够长，否则你很难刚好在它执行期间按下按键。

所以代码里故意写了：

```c
led_on();
delay_busy(5000000U);
led_off();
```

它的教学目的只有一个：

- 制造一个肉眼可见的低优先级中断执行窗口

## 8. HAL版代码讲解

HAL 版代码在：

- [hal/src/main.c](/home/myself/workspace/mcu/stm32/STM32F103C8T6/STM32F103C8T6_GUIDE/20_nvic_priority/hal/src/main.c)

### 8.1 `GPIO_InitTypeDef`

`GPIO_InitTypeDef` 是 HAL 用来描述 GPIO 配置的结构体。

本课里它出现两次：

- 配置 PC13 为输出
- 配置 PA0 为下降沿外部中断

例如：

```c
gpio.Mode = GPIO_MODE_IT_FALLING;
gpio.Pull = GPIO_PULLUP;
```

它本质上对应寄存器版里的：

- PA0 输入模式
- 内部上拉
- EXTI 下降沿触发

### 8.2 `HAL_TIM_Base_Init()`

这个 API 初始化 TIM2 的基本计数参数。

本课关键字段：

- `Prescaler = 7200 - 1`
- `Period = 20000 - 1`
- `CounterMode = TIM_COUNTERMODE_UP`

它本质上对应寄存器版：

```c
TIM2->PSC = 7200U - 1U;
TIM2->ARR = 20000U - 1U;
```

### 8.3 `HAL_TIM_Base_Start_IT()`

这个 API 做两件关键事：

- 启动 TIM2 计数
- 允许 TIM2 更新中断

本质上对应：

```c
TIM2->DIER |= TIM_DIER_UIE;
TIM2->CR1 |= TIM_CR1_CEN;
```

### 8.4 `HAL_NVIC_SetPriority()`

HAL 版设置优先级用：

```c
HAL_NVIC_SetPriority(EXTI0_IRQn, 0U, 0U);
HAL_NVIC_SetPriority(TIM2_IRQn, 2U, 0U);
```

第一个数字是抢占优先级。

本课只需要记住：

- `EXTI0` 抢占优先级 0，更高
- `TIM2` 抢占优先级 2，更低

### 8.5 `IRQHandler` 和 `Callback` 的关系

HAL 版里你会看到：

```c
void EXTI0_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0);
}
```

这不是最终业务逻辑。

它的作用是：

- 进入 HAL 的 EXTI 中断处理流程
- HAL 内部检查并清除 EXTI 标志
- 然后调用用户写的 `HAL_GPIO_EXTI_Callback()`

所以真正控制 LED 的代码在：

```c
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
```

TIM2 也是类似：

```c
void TIM2_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim2);
}
```

然后 HAL 再调用：

```c
HAL_TIM_PeriodElapsedCallback()
```

这就是 HAL 里常见的“IRQHandler 入口 + Callback 写业务”的结构。

## 9. 两种写法对比

| 对比点 | 寄存器版 | HAL版 |
|---|---|---|
| GPIO 配置 | 直接写 `CRL/CRH` | 填 `GPIO_InitTypeDef` |
| EXTI 配置 | 写 `AFIO/EXTI` 寄存器 | `GPIO_MODE_IT_FALLING` 封装 |
| TIM2 配置 | 写 `PSC/ARR/DIER/CR1` | `HAL_TIM_Base_Init()` + `HAL_TIM_Base_Start_IT()` |
| NVIC 配置 | `NVIC_SetPriority()` / `NVIC_EnableIRQ()` | `HAL_NVIC_SetPriority()` / `HAL_NVIC_EnableIRQ()` |
| 中断业务 | 直接写在 `xxx_IRQHandler` | 写在 HAL Callback |

两种写法做的是同一件事：

- 建立两个中断源
- 设置不同优先级
- 用高优先级 EXTI0 抢占低优先级 TIM2

## 10. 运行现象

正常现象：

1. 程序运行后，LED 每隔约 2 秒长亮一小段时间
2. 如果在 LED 熄灭时按 PA0，LED 会短暂亮一下
3. 如果在 LED 长亮期间按 PA0，LED 会立刻短暂熄灭再亮回去

第三个现象最关键。

因为这说明：

- CPU 原本正在 TIM2 中断里
- EXTI0 仍然能立刻插进来
- EXTI0 确实抢占了 TIM2

## 11. 常见问题排查

### 11.1 LED 完全不亮

先查：

- PC13 是否是板载 LED
- GPIOC 时钟是否打开
- PC13 是否配置成输出
- BluePill LED 是否低电平点亮

### 11.2 TIM2 不触发

按顺序查：

1. `RCC->APB1ENR` 是否打开 TIM2 时钟
2. `TIM2->PSC` / `TIM2->ARR` 是否配置
3. `TIM2->DIER.UIE` 是否置 1
4. `NVIC_EnableIRQ(TIM2_IRQn)` 是否调用
5. `TIM2_IRQHandler()` 里是否清 `UIF`

### 11.3 PA0 按键没反应

按顺序查：

1. PA0 是否接到 GND
2. PA0 是否配置成上拉输入
3. `AFIO->EXTICR[0]` 是否选择 PA0
4. `EXTI->IMR.MR0` 是否置 1
5. `EXTI->FTSR.TR0` 是否置 1
6. `NVIC_EnableIRQ(EXTI0_IRQn)` 是否调用

### 11.4 按键能触发，但看不出抢占

可能原因：

- 你没有在 TIM2 长亮窗口里按下
- TIM2 忙等待时间太短
- EXTI0 优先级没有高于 TIM2
- 两个中断优先级设置反了

## 12. 本课要点总结

本课最小可运行链路是：

```text
TIM2 更新中断作为低优先级长窗口
PA0 EXTI0 作为高优先级插入事件
NVIC 设置 EXTI0 高于 TIM2
观察 EXTI0 是否能打断 TIM2
```

真正要记住的是：

- 外设中断使能和 NVIC 中断使能是两道门
- `IRQn` 是 NVIC 用来识别中断源的编号
- 优先级数字越小，优先级越高
- 高优先级中断可以抢占低优先级中断
- HAL 的 Callback 不是魔法，它是在 HAL IRQHandler 清标志后被调用的用户入口

## 13. 扩展练习

1. 把 `EXTI0` 和 `TIM2` 的优先级交换，观察 LED 长亮期间按键是否还能插入短脉冲。
2. 把 TIM2 周期改成 1 秒，重新计算 `PSC` 和 `ARR`。
3. 把 EXTI0 的触发方式改成上升沿，观察按下和松开时谁触发。
4. 在调试器里观察 `EXTI->PR` 和 `TIM2->SR` 标志位。

## 14. 下一课预告

下一课学习独立看门狗 `IWDG`。

前面这些课的中断都是“外设提醒 CPU 做事”，而看门狗是另一种思路：

- CPU 必须周期性证明自己还活着
- 如果没有按时喂狗
- 硬件会主动复位系统

# 第 35 课：窗口看门狗 WWDG

## 1. 本课到底在学什么

本课表面现象是：程序启动 WWDG 窗口看门狗。按住 PA0 时，程序等待计数器进入合法窗口后刷新，系统正常运行；松开 PA0 后停止刷新，WWDG 很快复位 MCU。若上一次复位由 WWDG 引起，启动后 PC13 LED 会快闪 4 次。

真正要学的是 WWDG 和上一课 IWDG 的差别：

```text
IWDG：只要在超时前喂狗就行
WWDG：必须在规定窗口内喂狗，喂早也会复位，喂晚也会复位
```

本课完整链路是：

```text
APB1 时钟驱动 WWDG
  -> WWDG 7 位计数器从 0x7F 递减
  -> 窗口值 W=0x50 定义最早刷新时刻
  -> T > 0x50 时刷新：太早，复位
  -> 0x40 < T <= 0x50 时刷新：合法
  -> T <= 0x40 后接近超时，最终复位
```

窗口看门狗用于发现“程序跑得太慢”和“程序异常跑得太快/流程乱跳”两类问题。它比 IWDG 更严格，也更容易因为喂狗时机不对而复位。

## 2. 本课学习目标

学完本课，你应该能回答：

1. WWDG 和 IWDG 的时钟源有什么不同？
2. WWDG 的“窗口”到底是什么意思？
3. 为什么 WWDG 喂早也会复位？
4. `CR.WDGA` 和 `CR.T[6:0]` 分别控制什么？
5. `CFR.WDGTB` 和 `CFR.W[6:0]` 分别控制什么？
6. 为什么刷新 WWDG 是重写 `CR=WDGA|0x7F`？
7. `RCC->CSR.WWDGRSTF` 怎么判断上次复位来源？
8. HAL 版 `HAL_WWDG_Refresh()` 为什么仍要自己保证窗口时机？

## 3. 本课目录结构

```text
35_watchdog_wwdg/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

`reg/` 直接操作 `WWDG->CR/CFR` 和 `RCC->CSR`。  
`hal/` 使用 `WWDG_HandleTypeDef`、`HAL_WWDG_Init()`、`HAL_WWDG_Refresh()`。

## 4. 实验硬件

- 开发板：STM32F103C8T6 BluePill
- 下载器：ST-Link
- LED：PC13，常见 BluePill 为低电平点亮
- 按键：PA0 接按键，按下接 GND，内部上拉

按键逻辑：

- PA0 按下：等待 WWDG 进入窗口后刷新
- PA0 松开：不刷新，等待 WWDG 复位

## 5. 先建立一个最基本的脑图

```text
启动阶段
  -> 读取 RCC->CSR.WWDGRSTF
  -> 清除复位标志 RMVF
  -> 如果上次是 WWDG 复位，LED 快闪 4 次

初始化阶段
  -> 打开 GPIOC/ GPIOA
  -> PC13 输出，PA0 上拉输入
  -> 打开 WWDG APB1 时钟
  -> CFR 设置分频和窗口值 0x50
  -> CR 设置 WDGA=1 和初始计数器 0x7F

主循环
  -> 按住 PA0
  -> 读取 CR.T，等待 T <= 0x50
  -> 如果 T > 0x40，刷新到 0x7F
  -> LED 闪一下
  -> 松开 PA0 后不刷新
  -> T 递减到 0x3F 触发复位
```

注意：WWDG 超时时间比上一课 IWDG 短很多，本课约为百毫秒量级。调试时停在断点里很容易导致复位。

## 6. 先认识本课里出现的核心名词

### 6.1 `WWDG` 是什么

`WWDG` 是 Window Watchdog，窗口看门狗。

它属于芯片内部安全监督外设。和 IWDG 一样，它能在软件没有按时刷新时复位 MCU；不同点是它还限制“不能太早刷新”。

它控制的是系统是否被强制复位，以及刷新动作是否发生在合法时间窗口内。

### 6.2 `窗口` 是什么

窗口是 WWDG 允许刷新计数器的一段时间范围。

本课设置窗口值 `W=0x50`，计数器从 `0x7F` 往下递减：

- `T > 0x50`：太早，刷新会复位
- `0x40 < T <= 0x50`：合法窗口，可以刷新
- `T <= 0x40`：太晚，接近复位

这就是 WWDG 名字里 Window 的含义。

### 6.3 `APB1 时钟` 是什么

WWDG 使用 APB1 派生时钟，不像 IWDG 使用独立 LSI。

本课 PCLK1 为 36MHz，WWDG 计数时钟按公式：

```text
PCLK1 / 4096 / WDGTB分频
```

如果 APB1 时钟变化，WWDG 时间窗口也会变化。

### 6.4 `WWDG->CR` 是什么

`CR` 是 WWDG 控制寄存器。

本课关注：

- `WDGA`：启动 WWDG
- `T[6:0]`：7 位递减计数器

启动和刷新都要写 `CR`。刷新时必须保持 `WDGA=1`，并把计数器重装为 `0x7F`。

### 6.5 `WDGA` 是什么

`WDGA` 是 WWDG Activation 位。

写 1 后启动 WWDG。启动后软件不能关闭，只能在窗口内刷新或等待复位。

如果忘记置 `WDGA`，WWDG 根本不会运行。

### 6.6 `T[6:0]` 是什么

`T[6:0]` 是 WWDG 的 7 位计数器值。

它从初始值 `0x7F` 递减。软件读取它判断是否进入窗口；刷新时重新写入 `0x7F`。

当计数器跌到复位门限以下，WWDG 触发 MCU 复位。

### 6.7 `WWDG->CFR` 是什么

`CFR` 是 WWDG 配置寄存器。

本课关注：

- `WDGTB`：分频系数
- `W[6:0]`：窗口上限值
- `EWI`：提前唤醒中断，本课不用

`CFR` 决定计数速度和最早允许刷新点。

### 6.8 `WDGTB` 是什么

`WDGTB` 是 WWDG 时钟分频字段。

本课设置为分频 8。WWDG 计数频率大约：

```text
36MHz / 4096 / 8 ≈ 1098Hz
```

这让计数器每约 0.91ms 递减一次。

### 6.9 `窗口值 0x50` 是什么

`0x50` 是本课设置的窗口上限。

计数器刚从 `0x7F` 开始时大于 `0x50`，这时不能刷新。必须等它递减到 `0x50` 或以下，并且仍大于 `0x40`。

### 6.10 `WWDGRSTF` 是什么

`WWDGRSTF` 是 RCC 复位来源标志。

如果上一次复位由 WWDG 引起，`RCC->CSR.WWDGRSTF` 会置位。本课启动时读取它，并用 LED 快闪 4 次提示。

### 6.11 `RMVF` 是什么

`RMVF` 是复位标志清除位。

读取完复位来源后写 `RCC->CSR.RMVF` 清除标志，避免后续启动继续误判上一次复位原因。

### 6.12 `HAL_WWDG_Init()` 是什么

HAL 初始化 WWDG 的 API。

它根据 `hwwdg.Init.Prescaler/Window/Counter/EWIMode` 写 `CFR` 和 `CR`，并启动 WWDG。

### 6.13 `HAL_WWDG_Refresh()` 是什么

HAL 刷新 WWDG 的 API。

底层是重写 `WWDG->CR`，把计数器重新装到 `Counter`。它不会替你选择正确窗口时机，应用层仍要保证没有喂早。

## 7. 寄存器版代码逐步讲解

### 7.1 读取复位来源

```c
uint8_t was_wwdg_reset = (RCC->CSR & RCC_CSR_WWDGRSTF) != 0U;
RCC->CSR |= RCC_CSR_RMVF;
```

先读 `WWDGRSTF`，再清复位标志。若顺序反了，就会丢失本次判断依据。

### 7.2 初始化 LED 和 PA0

PC13 配成输出。PA0 配成上拉输入，按下接 GND，读到 0 表示按下。

### 7.3 WWDG 复位提示

如果上次是 WWDG 复位，LED 快闪 4 次。上一课 IWDG 是 3 次，本课用 4 次区分来源。

### 7.4 打开 WWDG 时钟

```c
RCC->APB1ENR |= RCC_APB1ENR_WWDGEN;
```

WWDG 挂在 APB1 上，必须打开时钟。IWDG 不需要这一步，因为它使用独立 LSI 时钟域。

### 7.5 配置 `CFR`

```c
WWDG->CFR = WWDG_CFR_WDGTB | WWDG_WINDOW_VALUE;
```

`WWDG_CFR_WDGTB` 设置分频 8，`WWDG_WINDOW_VALUE` 设置窗口上限 `0x50`。

### 7.6 启动 WWDG

```c
WWDG->CR = WWDG_CR_WDGA | WWDG_COUNTER_VALUE;
```

`WDGA=1` 启动 WWDG，`COUNTER=0x7F` 设置初始计数器值。启动后软件不能关闭。

### 7.7 `wwdg_refresh_in_window()` 等待窗口

```c
do {
    counter = WWDG->CR & WWDG_CR_T;
} while (counter > WWDG_WINDOW_VALUE);
```

当计数器还大于 `0x50` 时，说明刷新太早，函数继续等待。

### 7.8 判断是否太晚

```c
if (counter > 0x40U) {
    WWDG->CR = WWDG_CR_WDGA | WWDG_COUNTER_VALUE;
}
```

只有 `counter > 0x40` 时才刷新。若已经小于等于 `0x40`，离复位太近，说明软件错过窗口。

### 7.9 刷新动作为什么写整个 CR

刷新不是只写 `T` 字段，而是重写 `CR`。写入时必须包含 `WDGA`，否则不能保持看门狗激活状态。

### 7.10 主循环按键逻辑

按下 PA0 时，等待进入窗口再刷新并闪 LED。松开 PA0 时不刷新，LED 点亮，WWDG 很快复位。

## 8. HAL 版代码逐步讲解

### 8.1 `__HAL_RCC_WWDG_CLK_ENABLE()`

该宏打开 APB1 上的 WWDG 时钟，对应寄存器版 `RCC->APB1ENR.WWDGEN=1`。

### 8.2 `WWDG_HandleTypeDef hwwdg`

HAL 用句柄描述 WWDG 外设和配置。`hwwdg.Instance = WWDG` 绑定硬件外设。

### 8.3 `Prescaler`

`WWDG_PRESCALER_8` 对应 `CFR.WDGTB` 分频 8。

### 8.4 `Window`

`hwwdg.Init.Window = 0x50` 对应 `CFR.W[6:0]`，定义最早允许刷新的计数器值。

### 8.5 `Counter`

`hwwdg.Init.Counter = 0x7F` 对应启动和刷新时写入 `CR.T[6:0]` 的初始计数值。

### 8.6 `EWIMode`

`WWDG_EWI_DISABLE` 表示不使用提前唤醒中断。本课只演示复位，不演示 EWI。

### 8.7 `HAL_WWDG_Init()`

它写 `CFR` 和 `CR`，启动 WWDG。调用后 WWDG 已经开始递减计数。

### 8.8 `HAL_WWDG_Refresh()`

它刷新计数器，但不会替应用判断“是不是太早”。所以 HAL 版仍然先轮询 `WWDG->CR & WWDG_CR_T`，等到 `T<=0x50` 再刷新。

## 9. 两个版本真正应该怎么学

寄存器版重点看：

```text
CFR 设置窗口和分频
CR 启动和刷新
读 CR.T 判断窗口
RCC->CSR 判断复位来源
```

HAL 版重点看：

```text
Prescaler -> CFR.WDGTB
Window -> CFR.W
Counter -> CR.T
HAL_WWDG_Refresh -> 重写 CR
```

WWDG 不是“更短时间的 IWDG”，它多了喂早复位这一条约束。

## 10. 检验问题清单

### 10.1 WWDG 的窗口是什么意思？

**答**：窗口是允许刷新计数器的时间范围。本课只有当 `0x40 < T <= 0x50` 时刷新才合法。

### 10.2 为什么刚启动后不能马上刷新？

**答**：刚启动时计数器为 `0x7F`，大于窗口值 `0x50`。此时刷新属于喂早，会触发复位。

### 10.3 WWDG 和 IWDG 的时钟源有什么不同？

**答**：IWDG 使用独立 LSI；WWDG 使用 APB1 派生时钟，所以受 PCLK1 影响。

### 10.4 本课为什么要打开 WWDG 时钟？

**答**：WWDG 是 APB1 外设，访问和运行都需要 `RCC->APB1ENR.WWDGEN`。IWDG 不需要这个 APB 时钟使能。

### 10.5 刷新 WWDG 写哪个寄存器？

**答**：写 `WWDG->CR`，保持 `WDGA=1`，并把计数器重装为 `0x7F`。

### 10.6 `WWDGRSTF` 有什么用？

**答**：它记录上一次复位是否由 WWDG 引起。本课用它决定是否 LED 快闪 4 次。

### 10.7 HAL 版为什么仍然读 `WWDG->CR`？

**答**：因为应用必须知道是否进入窗口。`HAL_WWDG_Refresh()` 只是刷新动作，不负责决定刷新时机。

### 10.8 真实项目里 WWDG 适合检查什么？

**答**：适合检查主循环或关键流程是否在合理时间范围内运行，既不能卡太久，也不能异常跳过流程太快喂狗。

## 11. 工程实现步骤

### 11.1 需求分析

本课要演示 WWDG 的窗口限制，所以必须让程序先等待窗口再刷新，并通过松开按键停止刷新触发复位。

### 11.2 硬件核查

确认 PC13 LED 正常，PA0 按下接地，内部上拉有效。WWDG 本身不需要外部器件。

### 11.3 寄存器路线

读复位标志，初始化 GPIO，打开 WWDG APB1 时钟，配置 CFR，写 CR 启动，主循环按窗口条件刷新。

### 11.4 HAL 路线

用 RCC 宏读清复位标志，用 HAL GPIO 配按键/LED，用 `WWDG_HandleTypeDef` 配 Prescaler/Window/Counter，再在窗口内调用 `HAL_WWDG_Refresh()`。

### 11.5 工程思维

WWDG 的喂狗点应该放在关键流程完成之后，并且流程耗时应落在窗口范围内。否则喂早或喂晚都说明系统状态不可信。

### 11.6 常见工程陷阱

把 WWDG 当 IWDG 用、启动后立刻刷新、窗口设置太窄、调试断点停太久、忘开 APB1 WWDG 时钟，都会导致频繁复位。

## 12. 运行现象

按住 PA0 运行时，程序会等待窗口、合法刷新，LED 周期闪烁。松开 PA0 后 LED 点亮，WWDG 很快复位 MCU。复位后若检测到 WWDG 标志，LED 快闪 4 次。

## 13. 常见问题排查

### 13.1 一启动就复位

检查是否启动后立即刷新，或窗口值设置太低导致来不及进入合法窗口。

### 13.2 按住 PA0 仍复位

检查 PA0 是否读到低电平，窗口等待逻辑是否正确，LED 闪烁耗时是否让下一次刷新错过窗口。

### 13.3 松开 PA0 不复位

检查 WWDG 是否真正启动，APB1 WWDG 时钟是否打开，`CR.WDGA` 是否置位。

### 13.4 复位后没有快闪 4 次

检查是否先读 `WWDGRSTF` 再清 `RMVF`。如果先清标志，就无法判断复位来源。

### 13.5 调试时反复断开

WWDG 时间很短，断点停住后无法刷新，会复位。调试窗口看门狗代码时要小心断点位置。

## 14. 本课最核心的结论

1. WWDG 是窗口看门狗，喂早和喂晚都可能复位。
2. WWDG 使用 APB1 派生时钟，不是独立 LSI。
3. `CFR` 配置分频和窗口，`CR` 启动和刷新计数器。
4. 合法刷新窗口由 `T` 和 `W` 的关系决定。
5. `WWDGRSTF` 能判断上一次是否窗口看门狗复位。
6. HAL 版封装寄存器写入，但刷新时机仍由应用负责。

## 15. 建议你现在怎么读这节课

先把 IWDG 和 WWDG 对比表画出来，再读第 6 章的 `CR/CFR/T/W`。然后对照 `wwdg_refresh_in_window()`，确认它为什么先等窗口再刷新。

## 16. 扩展练习

1. 把窗口值从 `0x50` 改成 `0x60`，观察窗口变宽后的现象。
2. 故意去掉等待窗口，直接刷新，观察是否复位。
3. 开启 EWI 提前唤醒中断，在复位前点亮 LED。
4. 计算不同 `WDGTB` 分频下的窗口时间。

## 17. 下一课预告

- 上一课：[34_watchdog_iwdg](../34_watchdog_iwdg/README.md)
- 下一课：[36_bkp_backup_register](../36_bkp_backup_register/README.md)

# 第 23 课：RTC 基础

## 1. 本课到底在学什么

本课表面上是让 LED 每秒翻转一次，真正要学的是 STM32 的 RTC 计时链路：

```text
低速时钟源
  -> 备份域选择 RTC 时钟
  -> RTC 预分频
  -> 秒计数器递增
  -> 程序读取计数值并产生现象
```

RTC 和普通定时器不一样，它位于备份域，常用于实时时钟、掉电保持时间、低功耗唤醒等场景。

## 2. 学习目标

- 理解 RTC 为什么和备份域有关
- 理解 `PWR`、`BKP`、`BDCR` 为什么会出现在 RTC 初始化里
- 理解 `LSI/LSE` 与 RTC 的关系
- 掌握 RTC 预分频得到 1 秒节拍的基本方法
- 能对应 HAL RTC 初始化和底层寄存器配置

## 3. 目录结构

```text
23_rtc_basic/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

## 4. 硬件环境

- STM32F103C8T6 BluePill
- PC13 板载 LED
- 本课使用内部 `LSI`，不强依赖外部 32.768kHz 晶振

说明：LSI 精度较差，所以本课是 RTC 入门实验，不是高精度时钟实验。

## 5. 先建立脑图

```text
打开 PWR/BKP 时钟
  -> 允许访问备份域 DBP
  -> 启动 LSI
  -> BDCR 选择 LSI 作为 RTC 时钟
  -> 使能 RTC
  -> 等待 RTC 寄存器同步
  -> 进入 RTC 配置模式
  -> 设置 PRL 预分频
  -> 读取 CNT 秒计数器
```

## 6. 核心名词解释

### 6.1 备份域

备份域是一组特殊电路，包含 RTC、备份寄存器等。它和普通 APB 外设不同，因此修改 RTC 时钟源前要先允许访问备份域。

### 6.2 `PWR->CR.DBP`

`DBP` 是 Disable Backup domain write Protection。置 1 后，软件才允许写备份域相关寄存器。

### 6.3 `RCC->BDCR`

`BDCR` 是 Backup Domain Control Register，备份域控制寄存器。RTC 时钟源选择、RTC 使能、备份域复位都在这里。

### 6.4 `RTC->PRLH/PRLL`

这是 RTC 预分频寄存器。RTC 输入时钟经过预分频后产生秒节拍。本课用 LSI 约 40kHz，所以设置 `40000 - 1` 得到约 1 秒。

### 6.5 `RTC->CNTH/CNTL`

F1 的 RTC 计数器是 32 位，但分成高 16 位和低 16 位两个寄存器读取。读取时要注意高低位一致性。

## 7. 寄存器版代码讲解

寄存器版在 [reg/src/main.c](/home/myself/workspace/mcu/stm32/STM32F103C8T6/STM32F103C8T6_GUIDE/23_rtc_basic/reg/src/main.c)。

关键步骤：

1. `RCC->APB1ENR` 打开 PWR 和 BKP 时钟
2. `PWR->CR |= PWR_CR_DBP` 允许写备份域
3. `RCC->CSR |= RCC_CSR_LSION` 启动 LSI
4. `RCC->BDCR` 选择 LSI 并使能 RTC
5. 等待 `RTC_CRL_RSF`，确保 APB 侧寄存器同步
6. 进入配置模式，写 `PRLH/PRLL`
7. 主循环读取计数器，秒值变化就翻转 LED

## 8. HAL版代码讲解

HAL 版在 [hal/src/main.c](/home/myself/workspace/mcu/stm32/STM32F103C8T6/STM32F103C8T6_GUIDE/23_rtc_basic/hal/src/main.c)。

| HAL 写法 | 底层含义 |
|---|---|
| `HAL_PWR_EnableBkUpAccess()` | 置位 `PWR->CR.DBP` |
| `HAL_RCC_OscConfig()` | 打开 LSI |
| `HAL_RCCEx_PeriphCLKConfig()` | 在 `BDCR` 里选择 RTC 时钟 |
| `__HAL_RCC_RTC_ENABLE()` | 使能 RTC |
| `HAL_RTC_Init()` | 设置 RTC 预分频 |
| `HAL_RTC_GetTime()` | 读取 RTC 计数并换算成时间结构 |

## 9. 两种写法对比

寄存器版更适合理解备份域和同步等待；HAL 版让 RTC 初始化流程更集中。只要看到 RTC 初始化里出现 PWR/BKP/BDCR，就要想到“RTC 属于备份域，不是普通定时器”。

## 10. 运行现象

- LED 大约每 1 秒翻转一次
- 因为使用 LSI，实际时间可能偏快或偏慢
- 如果下载后 RTC 之前已使能，程序会沿用已有备份域状态

## 11. 常见问题排查

- LED 不闪：检查 LSI 是否 ready、RTC 是否使能
- 一直卡住：多半是等待 `RSF` 或 `RTOFF`，检查备份域访问权限
- 时间不准：LSI 误差正常，想更准应使用 LSE 32.768kHz 晶振
- 反复下载不重置 RTC：备份域可能保留了旧配置，可执行备份域复位

## 12. 本课总结

RTC 的重点不是“又一个定时器”，而是“低速时钟 + 备份域 + 秒计数器”。理解这三件事，后面学习低功耗唤醒和时间保持才不会断层。

## 13. 扩展练习

- 改用 LSE，预分频设置为 `32768 - 1`
- 每 10 秒闪烁一次 LED
- 使用备份寄存器记录程序启动次数

## 14. 下一课预告

下一课学习低功耗基础：让 CPU 进入 Sleep，并通过外部中断唤醒。

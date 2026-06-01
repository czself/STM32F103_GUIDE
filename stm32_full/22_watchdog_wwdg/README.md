# 第 22 课：窗口看门狗 WWDG

## 1. 本课到底在学什么

本课表面上仍然是看门狗复位实验，但真正学习的是“喂狗时间窗口”。

IWDG 只关心你有没有太久没喂狗；WWDG 还关心你是不是喂得太早。这样可以捕捉两类错误：

- 程序卡死，太晚喂狗
- 程序跑飞到异常路径，过早喂狗

## 2. 学习目标

- 理解 WWDG 和 IWDG 的区别
- 理解 `CR.T` 递减计数器和 `CFR.W` 窗口值
- 知道为什么 WWDG 必须挂在 APB1 时钟下
- 能配置一个“进入窗口后才刷新”的 Demo
- 理解 `HAL_WWDG_Refresh()` 为什么必须在合适时机调用

## 3. 目录结构

```text
22_watchdog_wwdg/
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
- PA0 外接按键到 GND

## 5. 先建立脑图

```text
启动 WWDG
  -> 配置分频 WDGTB
  -> 配置窗口值 W
  -> 写 CR.WDGA 启动
  -> 计数器 T 从 0x7F 递减

刷新判断
  -> T > W：太早刷新，会复位
  -> 0x40 < T <= W：窗口内刷新，合法
  -> T <= 0x40：太晚，会复位
```

本课 Demo 选择按住 PA0 时等待进入窗口后刷新；松开 PA0 时停止刷新，让它超时复位。

## 6. 核心名词解释

### 6.1 `WWDG`

`WWDG` 是 Window Watchdog，窗口看门狗。它由 APB1 时钟驱动，不像 IWDG 那样使用独立 LSI。

### 6.2 `WWDG->CR`

`CR` 是控制寄存器：

- `WDGA`：窗口看门狗激活位，一旦置 1 就启动
- `T[6:0]`：当前递减计数器值

刷新 WWDG 本质上就是重新写 `CR.T`。

### 6.3 `WWDG->CFR`

`CFR` 是配置寄存器：

- `WDGTB`：分频系数
- `W[6:0]`：窗口上限
- `EWI`：早期唤醒中断，本课先不使用

### 6.4 窗口值

本课使用：

- 初始计数器：`0x7F`
- 窗口值：`0x50`

也就是说，计数器刚从 `0x7F` 往下数时不能刷新，必须等它小于等于 `0x50` 后再刷新。

## 7. 寄存器版代码讲解

寄存器版在 [reg/src/main.c](/home/myself/workspace/mcu/stm32/STM32F103C8T6/STM32F103C8T6_GUIDE/22_watchdog_wwdg/reg/src/main.c)。

关键步骤：

1. `RCC->APB1ENR |= RCC_APB1ENR_WWDGEN` 打开 WWDG 时钟
2. `WWDG->CFR = WWDG_CFR_WDGTB | 0x50` 配置分频和窗口
3. `WWDG->CR = WWDG_CR_WDGA | 0x7F` 启动并装入初始计数
4. 主循环读取 `WWDG->CR & WWDG_CR_T`
5. 只有当前计数器进入窗口后才重新写 `WWDG->CR`

## 8. HAL版代码讲解

HAL 版在 [hal/src/main.c](/home/myself/workspace/mcu/stm32/STM32F103C8T6/STM32F103C8T6_GUIDE/22_watchdog_wwdg/hal/src/main.c)。

| HAL 写法 | 底层含义 |
|---|---|
| `__HAL_RCC_WWDG_CLK_ENABLE()` | 打开 APB1 上的 WWDG 时钟 |
| `hwwdg.Init.Prescaler` | 配置 `CFR.WDGTB` |
| `hwwdg.Init.Window` | 配置 `CFR.W` |
| `hwwdg.Init.Counter` | 配置初始 `CR.T` |
| `HAL_WWDG_Refresh()` | 在窗口内重装 `CR.T` |

## 9. 两种写法对比

寄存器版能直接看到 `T` 和 `W` 的比较关系；HAL 版把初始化参数集中到 `WWDG_HandleTypeDef`。两者都必须遵守窗口规则。

## 10. 运行现象

- 按住 PA0：程序等待合法窗口再刷新，LED 周期闪烁
- 松开 PA0：停止刷新，稍后复位
- 如果上次是 WWDG 复位，启动后 LED 快闪 4 次

## 11. 常见问题排查

- 一启动就复位：可能刷新太早，或窗口值设置过低
- 按住也复位：检查等待窗口逻辑是否真的等到 `T <= W`
- 完全不复位：检查 `WDGA` 是否置 1
- 时间不符合预期：WWDG 时钟来自 PCLK1，先确认 APB1 分频

## 12. 本课总结

WWDG 的核心是“必须在正确时间窗口内证明程序还活着”。它比 IWDG 更适合发现程序时序异常。

## 13. 扩展练习

- 把窗口值从 `0x50` 改成 `0x60`，观察可刷新窗口变化
- 故意在窗口外刷新，观察是否立即复位
- 加入 EWI 早期唤醒中断，在复位前点亮 LED

## 14. 下一课预告

下一课学习 RTC。它会引入备份域、低速时钟和“掉电后仍可保留时间”的系统级概念。

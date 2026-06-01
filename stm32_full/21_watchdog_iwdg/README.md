# 第 21 课：独立看门狗 IWDG

## 1. 本课到底在学什么

本课表面上是在做“按住按键不复位，松开按键会复位”的实验，真正学习的是嵌入式系统里的自恢复机制。

正常程序会周期性喂狗；如果程序跑飞、卡死、忘记喂狗，独立看门狗会强制复位 MCU。它的关键特点是：`IWDG` 使用独立的 `LSI` 时钟，所以主系统时钟出问题时，它仍然可能继续工作。

## 2. 学习目标

- 理解 `IWDG` 为什么叫“独立”看门狗
- 理解 `KR`、`PR`、`RLR`、`SR` 这些寄存器的作用
- 掌握“配置超时时间”和“喂狗”的基本方法
- 能通过复位标志判断上次是否由 IWDG 复位
- 理解 HAL 的 `HAL_IWDG_Init()` 和 `HAL_IWDG_Refresh()` 对应哪些底层动作

## 3. 目录结构

```text
21_watchdog_iwdg/
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
- PA0 外接按键到 GND，程序使用内部上拉

## 5. 先建立脑图

```text
配置 IWDG
  -> 打开写权限 KR=0x5555
  -> 设置预分频 PR
  -> 设置重装值 RLR
  -> KR=0xAAAA 先喂一次
  -> KR=0xCCCC 启动 IWDG

主循环
  -> 按住 PA0：周期性 KR=0xAAAA，系统不复位
  -> 松开 PA0：停止喂狗
  -> IWDG 倒计时到 0
  -> MCU 复位
```

## 6. 核心名词解释

### 6.1 `IWDG`

`IWDG` 是 Independent Watchdog，独立看门狗。它不依赖 APB 外设时钟，而是由内部低速时钟 `LSI` 驱动。

### 6.2 `LSI`

`LSI` 是 Low Speed Internal oscillator，内部低速 RC 振荡器，典型约 40kHz。它精度不高，但独立性好，适合看门狗。

### 6.3 `KR`

`KR` 是 Key Register，键寄存器。IWDG 用特定钥匙值控制关键动作：

- `0x5555`：允许修改 `PR` 和 `RLR`
- `0xAAAA`：重装计数器，也就是喂狗
- `0xCCCC`：启动 IWDG

### 6.4 `PR` 和 `RLR`

`PR` 是预分频寄存器，决定 LSI 先被分频多少。`RLR` 是重装寄存器，决定计数器从多少开始倒数。

本课配置约为：

```text
LSI 约 40000Hz
预分频 32 -> 约 1250Hz
RLR = 2500
超时时间约 2500 / 1250 = 2 秒
```

### 6.5 复位标志

`RCC->CSR` 中的 `IWDGRSTF` 可以判断上一次复位是否由 IWDG 引起。读完后要用 `RMVF` 清除，否则下次会误判。

## 7. 寄存器版代码讲解

寄存器版在 [reg/src/main.c](/home/myself/workspace/mcu/stm32/STM32F103C8T6/STM32F103C8T6_GUIDE/21_watchdog_iwdg/reg/src/main.c)。

关键步骤：

1. 读取 `RCC->CSR & RCC_CSR_IWDGRSTF` 判断上次是否看门狗复位
2. 写 `RCC_CSR_RMVF` 清复位标志
3. 写 `IWDG->KR = 0x5555` 解锁配置
4. 设置 `IWDG->PR` 和 `IWDG->RLR`
5. 等待 `IWDG->SR` 中的更新标志清零
6. 写 `0xAAAA` 喂狗，再写 `0xCCCC` 启动

注意：IWDG 一旦启动，软件不能关闭，只能继续喂狗或等待复位。

## 8. HAL版代码讲解

HAL 版在 [hal/src/main.c](/home/myself/workspace/mcu/stm32/STM32F103C8T6/STM32F103C8T6_GUIDE/21_watchdog_iwdg/hal/src/main.c)。

| HAL 写法 | 底层含义 |
|---|---|
| `hiwdg.Instance = IWDG` | 选择 IWDG 外设 |
| `hiwdg.Init.Prescaler` | 配置 `IWDG->PR` |
| `hiwdg.Init.Reload` | 配置 `IWDG->RLR` |
| `HAL_IWDG_Init()` | 解锁、写 PR/RLR、启动 IWDG |
| `HAL_IWDG_Refresh()` | 向 `KR` 写 `0xAAAA` |

## 9. 两种写法对比

寄存器版能看见“钥匙值”的意义，HAL 版更适合工程使用。无论哪种写法，本质都是让硬件倒计时，并在程序健康时周期性重装计数器。

## 10. 运行现象

- 按住 PA0：LED 周期闪烁，系统持续运行
- 松开 PA0：LED 亮住，约 2 秒后系统复位
- 复位后如果检测到 IWDG 复位，LED 快速闪 3 次

## 11. 常见问题排查

- 不复位：检查是否仍在喂狗，或 `KR=0xCCCC` 是否执行
- 一上电就复位：检查喂狗周期是否超过超时时间
- 看不出复位来源：检查是否读取并清除 `RCC->CSR` 复位标志
- 超时时间不精确：LSI 本身误差较大，这是正常现象

## 12. 本课总结

IWDG 的核心不是“定时器”，而是“程序健康证明”。程序必须周期性喂狗，否则硬件认为系统失控并复位。

## 13. 扩展练习

- 把预分频改为 64，观察复位时间变化
- 在主循环里故意写死循环，验证 IWDG 能否恢复系统
- 把 PA0 逻辑改成“按下停止喂狗”

## 14. 下一课预告

下一课学习窗口看门狗 `WWDG`。它比 IWDG 更严格：不仅不能喂晚，也不能喂早。

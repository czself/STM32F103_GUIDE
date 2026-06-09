# 第 37 课：RTC 基础

## 1. 本课到底在学什么

本课表面现象是：RTC 使用 LSI 作为时钟源，约每 1 秒计数一次；程序轮询 RTC 计数器，计数变化时翻转 PC13 LED。

真正要学的是 RTC 不只是“另一个定时器”。它位于备份域，配置前要解锁备份域，选择低速时钟源，等待跨时钟域同步，再进入配置模式写预分频和计数器。

本课链路是：

```text
打开 PWR/BKP 时钟
  -> DBP 解锁备份域
  -> 启动 LSI
  -> 备份域选择 LSI 作为 RTC 时钟
  -> RTCEN 使能 RTC
  -> 等待 RSF 同步
  -> 进入 CNF 配置模式
  -> PRL=40000-1 得到约 1Hz
  -> 读取 CNT 变化
  -> LED 每秒翻转
```

这节课先用 LSI，优点是不需要外部 32.768kHz 晶振；缺点是精度较差，所以 LED 翻转周期只是约 1 秒。

## 2. 本课学习目标

学完本课，你应该能回答：

1. RTC 为什么位于备份域？
2. 为什么配置 RTC 前要先设置 `PWR->CR.DBP`？
3. LSI、LSE、HSE/128 这几个 RTC 时钟源有什么区别？
4. `RCC->BDCR.RTCSEL` 和 `RTCEN` 分别做什么？
5. `RTC->CRL.RSF` 为什么必须等待？
6. `RTC->CRL.CNF` 和 `RTOFF` 分别控制什么？
7. `PRLH/PRLL` 为什么能把 LSI 分成 1Hz？
8. 为什么读取 `CNTH/CNTL` 要保证高低位一致？
9. HAL 版 `HAL_RTC_Init()` 对应哪些寄存器动作？

## 3. 本课目录结构

```text
37_rtc_basic/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

`reg/` 直接操作 PWR、BKP、RCC BDCR、RTC CRL/PRL/CNT。  
`hal/` 使用 HAL RCC、PWR、RTC 结构体完成同样配置。

## 4. 实验硬件

- 开发板：STM32F103C8T6 BluePill
- 下载器：ST-Link
- LED：PC13

本课使用内部 LSI，不需要外部 32.768kHz 晶振。若要做准确实时时钟，后续应改用 LSE。

## 5. 先建立一个最基本的脑图

```text
备份域权限
  -> 开 PWR/BKP
  -> DBP=1

RTC 时钟
  -> LSI ON
  -> 等 LSIRDY
  -> BDCR 选择 RTCSEL=LSI
  -> RTCEN=1

RTC 配置
  -> 等 RSF 同步
  -> 等 RTOFF
  -> CNF=1
  -> PRL=39999
  -> CNT=0
  -> CNF=0
  -> 等 RTOFF

运行
  -> 反复读 32 位 CNT
  -> CNT 变化说明过了约 1 秒
  -> PC13 翻转
```

## 6. 先认识本课里出现的核心名词

### 6.1 `RTC` 是什么

`RTC` 是 Real-Time Clock，实时时钟。它属于备份域里的计时外设，用低速时钟持续计数。

它和普通 TIM 不同：RTC 设计目标是长时间计时和低功耗保持，而不是高速 PWM、输入捕获这类控制任务。

### 6.2 `备份域` 是什么

备份域包含 RTC 和 BKP 备份寄存器。它有单独保护机制和可选 VBAT 供电。

配置 RTC 时必须先解锁备份域，否则写 RTC 时钟源、预分频和计数器可能不生效。

### 6.3 `PWR->CR.DBP` 是什么

`DBP` 是 Disable Backup domain Protection。

置 1 后允许写备份域。它对应上一课 BKP 写入前的同一个保护开关。

### 6.4 `LSI` 是什么

`LSI` 是内部低速振荡器，典型约 40kHz。

本课选择它作为 RTC 时钟源，因为无需外部晶振。缺点是误差较大，所以计时只能作为教学演示。

### 6.5 `LSE` 是什么

`LSE` 是外部低速晶振，通常为 32.768kHz。

它更适合真实时钟应用，但需要板子焊接晶振和负载电容。本课不使用 LSE，避免硬件依赖。

### 6.6 `RCC->BDCR` 是什么

`BDCR` 是 Backup Domain Control Register，备份域控制寄存器。

本课用它复位备份域、选择 RTC 时钟源、使能 RTC。它属于 RCC 里专门管理备份域时钟的寄存器。

### 6.7 `RTCSEL` 是什么

`RTCSEL` 是 RTC 时钟源选择字段。

本课设置为 LSI。若选错时钟源，例如选 LSE 但板上没有晶振，RTC 可能不走。

### 6.8 `RTCEN` 是什么

`RTCEN` 是 RTC 使能位。

选择好时钟源后，置位 `RTCEN` 才真正让 RTC 时钟送入 RTC 模块。

### 6.9 `RSF` 是什么

`RSF` 是 Registers Synchronization Flag。

RTC 在低速备份域时钟，CPU 在 APB 时钟域。读取 RTC 寄存器前要等待同步完成，`RSF=1` 表示 APB 侧读到的值可信。

### 6.10 `RTOFF` 是什么

`RTOFF` 表示 RTC 上一次写操作已经完成。

修改 RTC 配置前后都要等待它，避免连续写入跨时钟域寄存器时发生覆盖或未生效。

### 6.11 `CNF` 是什么

`CNF` 是 RTC 配置模式位。

`CNF=1` 后可以修改 `PRL` 和 `CNT`；写完后清 `CNF` 退出配置模式，硬件开始应用配置。

### 6.12 `PRLH / PRLL` 是什么

它们组成 RTC 预分频重装值 `PRL`。

本课设置 `PRL=40000-1`，用约 40kHz LSI 分出约 1Hz，使 RTC 计数器约每秒加 1。

### 6.13 `CNTH / CNTL` 是什么

RTC 计数器是 32 位，但 F1 分成高 16 位 `CNTH` 和低 16 位 `CNTL`。

读取时要防止低 16 位刚好溢出导致高低位不一致。本课用读高、读低、再读高的方法保证一致性。

### 6.14 `RTC_HandleTypeDef` 是什么

HAL 版用 `RTC_HandleTypeDef hrtc` 描述 RTC。

`AsynchPrediv=40000-1` 对应寄存器版 PRL；`Instance=RTC` 绑定 RTC 外设。

## 7. 寄存器版代码逐步讲解

### 7.1 LED 初始化

PC13 作为 RTC 秒变化的可见反馈。它不属于 RTC，只是现象层输出。

### 7.2 打开 PWR/BKP 并解锁

```c
RCC->APB1ENR |= RCC_APB1ENR_PWREN | RCC_APB1ENR_BKPEN;
PWR->CR |= PWR_CR_DBP;
```

这一步延续上一课备份域访问链路。

### 7.3 启动 LSI

```c
RCC->CSR |= RCC_CSR_LSION;
while ((RCC->CSR & RCC_CSR_LSIRDY) == 0U) {}
```

先打开 LSI 并等待稳定。没有稳定时不能作为可靠 RTC 时钟源。

### 7.4 判断 RTC 是否已使能

如果 `BDCR.RTCEN=0`，说明需要首次配置；如果已经为 1，只等待同步，避免重复复位备份域。

### 7.5 复位备份域

首次配置时先置位再清除 `BDRST`，让备份域进入干净状态。注意这会清掉 RTC 和 BKP 数据。

### 7.6 选择 LSI 并使能 RTC

```c
RCC->BDCR |= RCC_BDCR_RTCSEL_LSI;
RCC->BDCR |= RCC_BDCR_RTCEN;
```

`RTCSEL` 选时钟源，`RTCEN` 让 RTC 开始接收时钟。

### 7.7 等待 RSF 同步

清 `RSF` 后等待它重新置位，表示 APB 侧 RTC 寄存器已经同步。

### 7.8 进入配置模式

`rtc_enter_config()` 先等 `RTOFF=1`，再置 `CNF=1`。这样可以安全写 PRL 和 CNT。

### 7.9 配置 PRL 和 CNT

`PRLL=39999`，`PRLH=0`，得到约 1Hz。`CNTH/CNTL=0` 把计数器清零。

### 7.10 退出配置模式

清 `CNF`，再等 `RTOFF=1`，确保配置写入完成。

### 7.11 读取 32 位计数器

`rtc_get_counter()` 读高、读低、再读高。若两次高位相等，说明读取期间没有跨 16 位溢出。

### 7.12 主循环轮询

程序没有用中断，而是轮询 RTC 计数器。只要计数值变化，就认为过了一个 RTC 秒，然后翻转 LED。

## 8. HAL 版代码逐步讲解

### 8.1 `HAL_PWR_EnableBkUpAccess()`

对应设置 `PWR->CR.DBP=1`，允许配置备份域。

### 8.2 `HAL_RCC_OscConfig()`

HAL 版用 `RCC_OscInitTypeDef` 打开 LSI，对应寄存器版 `LSION/LSIRDY`。

### 8.3 `HAL_RCCEx_PeriphCLKConfig()`

用 `RCC_PeriphCLKInitTypeDef` 选择 RTC 时钟源为 LSI，对应 `BDCR.RTCSEL`。

### 8.4 `__HAL_RCC_RTC_ENABLE()`

对应设置 `BDCR.RTCEN=1`，使能 RTC。

### 8.5 `HAL_RTC_Init()`

根据 `hrtc.Init.AsynchPrediv` 配置 RTC 预分频，内部处理同步和配置模式。

### 8.6 `HAL_RTC_GetTime()`

读取 RTC 计数并换算成时分秒。本课只比较 `time.Seconds` 是否变化。

### 8.7 `RTC_FORMAT_BIN`

表示 HAL 返回二进制数，而不是 BCD 编码。这样 `time.Seconds` 可直接作为普通数字比较。

### 8.8 HAL 版轮询秒变化

先保存初始秒值，循环读取。秒值变化时翻转 LED，对应寄存器版比较 `rtc_get_counter()`。

## 9. 两个版本真正应该怎么学

寄存器版重点看：

```text
DBP -> LSI -> BDCR -> RSF -> CNF/RTOFF -> PRL/CNT
```

HAL 版重点看：

```text
PWR 解锁 -> OscConfig LSI -> PeriphCLK RTCSEL -> RTC Init -> GetTime
```

RTC 的难点是备份域和同步，不是 LED 翻转本身。

## 10. 检验问题清单

### 10.1 RTC 和 TIM2 最大区别是什么？

**答**：RTC 位于备份域，用低速时钟持续计数，适合长时间和低功耗保持；TIM2 是普通定时器外设。

### 10.2 为什么要设置 DBP？

**答**：因为 RTC 位于备份域，写备份域前必须关闭写保护，否则配置可能不生效。

### 10.3 本课为什么用 LSI？

**答**：LSI 是内部低速时钟，不需要外部晶振，适合教学验证。但精度不高。

### 10.4 `RSF=1` 表示什么？

**答**：表示 RTC 寄存器已经同步到 APB 侧，CPU 读到的 RTC 值可信。

### 10.5 配置 PRL 前为什么要进入 CNF？

**答**：RTC 的预分频和计数器需要在配置模式下修改，`CNF=1` 才允许写这些寄存器。

### 10.6 为什么读取 CNT 要读两次高位？

**答**：防止低 16 位溢出时高低位不一致。两次高位相等说明读取期间没有跨越高位变化。

### 10.7 HAL 的 `AsynchPrediv` 对应什么？

**答**：对应 RTC 预分频重装值 PRL。本课为 `40000-1`。

### 10.8 LED 翻转不准一秒正常吗？

**答**：正常。LSI 精度较差，实际周期可能偏快或偏慢。

## 11. 工程实现步骤

### 11.1 需求分析

本课要验证 RTC 可以用低速时钟独立计数，并用 LED 显示秒变化。

### 11.2 硬件核查

确认 PC13 LED 正常。本课使用 LSI，无需外部晶振。若改用 LSE，要确认板子有 32.768kHz 晶振。

### 11.3 寄存器路线

解锁备份域，启动 LSI，选择 RTC 时钟源，使能 RTC，等待同步，配置 PRL/CNT，轮询计数器。

### 11.4 HAL 路线

用 HAL PWR 解锁，用 RCC HAL 配 LSI 和 RTC 时钟，用 `HAL_RTC_Init()` 配预分频，用 `HAL_RTC_GetTime()` 读秒。

### 11.5 工程思维

RTC 初始化通常要用 BKP 标记判断是否已经配置过，避免每次启动都清零时间。本课代码用 `RTCEN` 做最小判断。

### 11.6 常见工程陷阱

忘记 DBP、忘等 RSF、没进 CNF 就写 PRL、误以为 LSI 很准、重复复位备份域导致时间丢失，都是 RTC 常见问题。

## 12. 运行现象

PC13 LED 约每秒翻转一次。由于使用 LSI，实际间隔可能不是精确 1 秒。

若 RTC 已经运行，复位后程序不会重新复位备份域，只等待同步后继续读计数。

## 13. 常见问题排查

### 13.1 LED 不翻转

检查 LSI 是否启动、RTCEN 是否置位、RSF 是否同步完成、PRL 是否配置成功。

### 13.2 每次复位计数都清零

检查是否每次都执行了 `BDRST` 或写 `CNTH/CNTL=0`。真实项目应使用 BKP 标记避免重复初始化。

### 13.3 周期不准

LSI 精度差是正常原因。需要准确时间应使用 LSE。

### 13.4 HAL 版 `GetTime` 不变化

检查 `HAL_RCCEx_PeriphCLKConfig()` 是否选择了 RTC 时钟源，`__HAL_RCC_RTC_ENABLE()` 是否调用。

### 13.5 写 RTC 配置无效

检查 PWR/BKP 时钟和 `HAL_PWR_EnableBkUpAccess()` 或 `PWR->CR.DBP`。

## 14. 本课最核心的结论

1. RTC 位于备份域，配置前必须解锁备份域。
2. 本课使用 LSI，无需外部晶振但精度不高。
3. `BDCR` 负责 RTC 时钟源选择和 RTC 使能。
4. 读取 RTC 前要等待 `RSF` 同步。
5. 修改 PRL/CNT 要进入 `CNF` 配置模式并等待 `RTOFF`。
6. RTC 32 位计数器高低位读取要保证一致性。
7. HAL RTC API 封装了同步和配置模式，但底层链路相同。

## 15. 建议你现在怎么读这节课

先复习上一课 BKP 的备份域权限，再读 RTC 初始化。把 `DBP/LSI/BDCR/RSF/CNF/RTOFF/PRL/CNT` 按顺序写下来，代码就顺了。

## 16. 扩展练习

1. 用 BKP 寄存器保存“RTC 已初始化”标记。
2. 改用 LSE，比较 LED 周期稳定性。
3. 把 RTC 计数器初值设置为 100，观察启动后变化。
4. 增加 RTC 秒中断，而不是主循环轮询。

## 17. 下一课预告

- 上一课：[36_bkp_backup_register](../36_bkp_backup_register/README.md)
- 下一课：[38_flash_internal](../38_flash_internal/README.md)

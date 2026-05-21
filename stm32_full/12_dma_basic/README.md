# 第 12 课：DMA 基础

## 1. 本课要学什么

这一课我们正式学习 `DMA`。

前一课 `11_adc_interrupt` 的数据流是：

1. ADC 完成一次转换
2. 产生中断
3. CPU 进入中断函数
4. CPU 从 `ADC1->DR` 里把结果读出来

这一课要把"CPU 自己去搬数据"这一步拿掉，改成：

1. ADC 完成一次转换
2. **DMA 自动**把 `ADC1->DR` 的值搬到内存变量里
3. CPU 只需要读取这个内存变量

也就是说，这一课的重点不是"ADC 怎么采样"，而是：

- 什么是 DMA
- 为什么 DMA 能替 CPU 搬数据
- DMA 是怎么知道"从哪里搬、搬到哪里、搬多少次"的
- STM32F103 里 ADC1 为什么要配合 `DMA1_Channel1`
- `ADC1->CR2` 中的 `CONT` 和 `DMA` 位是干什么的
- HAL 的 `__HAL_LINKDMA()` 和 `HAL_ADC_Start_DMA()` 如何配合

### DMA vs 中断 vs 轮询

| 特性 | 轮询 (10_adc_polling) | 中断 (11_adc_interrupt) | DMA (本课) |
|------|----------------------|------------------------|-----------|
| CPU 参与度 | 全程等待 EOC | 中断时读一次 DR | **完全不需要碰 DR** |
| 数据搬运 | CPU 亲自读 DR | CPU 在中断中读 DR | **DMA 自动搬运** |
| 主循环 | 被采样循环占用 | 空闲时可做其他任务 | **CPU 完全解放** |
| 代码复杂度 | 低 | 中 | 中（需要配 DMA） |

---

## 2. 本课最终 Demo

### 2.1 硬件连接

- `PA1` 接 10k 电位器滑动端
- 电位器两端分别接 `3.3V` 和 `GND`
- `PC13` 使用核心板板载 LED

### 2.2 运行现象

- ADC 持续采样 `PA1` 的模拟电压
- DMA 自动把每次采样结果搬到内存变量 `g_adc_value`
- 主循环只看 `g_adc_value`
- 当 `g_adc_value > 2048` 时，点亮 `PC13`
- 当 `g_adc_value <= 2048` 时，熄灭 `PC13`

这样你会看到：

- 电位器电压变化时，LED 会跟着阈值变化
- 这次 CPU 不需要轮询 `EOC`
- 也不需要进入 ADC 中断后再读 `DR`

---

## 3. 先建立一个总图

这一课的数据流如下：

```
PA1 模拟电压 → ADC1 持续转换 → ADC1->DR
    ↓（DMA 请求）
DMA1_Channel1 自动搬运
    ↓
g_adc_value（内存变量）
    ↓
CPU 读取比较 → 控制 LED
```

这条链路里每一段的意思是：

- `PA1 模拟电压`：来自外部电位器
- `ADC1`：负责把模拟电压转换成数字值
- `ADC1->DR`：ADC 的数据寄存器，转换结果出现在这里
- `DMA1_Channel1`：不经过 CPU，自动把 `DR` 里的值搬到内存
- `g_adc_value`：RAM 里的变量，DMA 搬过来的最新结果
- `主循环判断`：CPU 只需读取这个变量，不再读 ADC 寄存器

---

## 4. 什么是 DMA

`DMA` 全称是 `Direct Memory Access`，中文叫"直接存储器访问"。

你可以先把它理解成：

**DMA 是 MCU 里专门负责搬数据的小搬运工。**

CPU 平时可以做很多事情：

- 算法处理
- 通信协议
- 状态机控制
- 中断响应

如果每次外设出一点数据，都让 CPU 去手动搬一次，CPU 就会被很多重复劳动占住。

DMA 的作用就是：

- 你先告诉它源地址（CPAR）
- 再告诉它目标地址（CMAR）
- 再告诉它搬多少次（CNDTR）
- 再告诉它数据宽度（PSIZE/MSIZE）
- 再告诉它搬运方向（DIR）
- 再告诉它是否循环（CIRC）

之后 DMA 就会自己干活。

---

## 5. 为什么 ADC 特别适合配 DMA

ADC 是一个"会不断产生数据"的外设。

例如本课里：

- 电位器电压一直存在
- ADC 会持续转换（CONT=1）
- 每完成一次转换，`ADC1->DR` 都会出现一个新结果

如果不用 DMA，那么每次都要：

- 等 `EOC`
- 读 `DR`
- 再等下一次

这非常适合交给 DMA——DMA 可以每次都在 ADC 转换完成后自动读 DR。

所以"ADC + DMA"是 STM32 里最经典的组合之一。

---

## 6. 本课寄存器版会遇到哪些新寄存器

### 6.1 ADC 侧

| 寄存器 | 位 | 本课值 | 作用 |
|--------|-----|-------|------|
| `ADC1->CR2` | CONT（bit 1） | 1 | **连续转换模式**：启动后 ADC 一直转 |
| `ADC1->CR2` | DMA（bit 8） | 1 | **DMA 请求使能**：每次 EOC 发 DMA 请求 |

### 6.2 DMA 侧

| 寄存器 | 本课写入值 | 作用 |
|--------|-----------|------|
| `DMA1_Channel1->CPAR` | `&ADC1->DR` | **外设地址**：DMA 从哪读数据 |
| `DMA1_Channel1->CMAR` | `&g_adc_value` | **内存地址**：DMA 写到哪 |
| `DMA1_Channel1->CNDTR` | 1 | **传输计数**：每轮搬 1 个数据 |
| `DMA1_Channel1->CCR` | DIR=0, CIRC=1, MINC=0, PSIZE/MSIZE=01 | **控制配置** |

#### 6.2.1 `CCR` 各字段详解

| CCR 字段 | 位 | 本课值 | 含义 |
|----------|-----|-------|------|
| DIR | bit 4 | 0 | **方向**：外设 → 内存 |
| CIRC | bit 5 | 1 | **循环模式**：搬完后自动重载，持续工作 |
| PINC | bit 6 | 0 | **外设地址不自增**：始终读 `ADC1->DR` |
| MINC | bit 7 | 0 | **内存地址不自增**：始终写 `g_adc_value` |
| PSIZE[1:0] | bit 8-9 | 01 | **外设宽度**：16 位（半字） |
| MSIZE[1:0] | bit 10-11 | 01 | **内存宽度**：16 位（半字） |
| PL[1:0] | bit 12-13 | 10 | **优先级**：高 |

---

## 7. 为什么是 DMA1_Channel1

这不是随便选的。

在 STM32F103 里，很多外设和 DMA 通道之间是**固定映射关系**。

对于 `ADC1` 而言：

- `ADC1` 对应 `DMA1_Channel1`

所以你不能拿 `DMA1_Channel3` 或 `DMA1_Channel5` 来接 ADC1。

这类映射关系一定要查参考手册的 DMA 请求映射表。

---

## 8. 本课寄存器版配置顺序

1. 配系统时钟到 `72MHz`
2. 初始化 `PC13` 作为 LED 输出
3. 初始化 `PA1` 为模拟输入
4. **开启 DMA1 时钟**（RCC->AHBENR.DMA1EN）
5. 配置 `DMA1_Channel1`
   - CPAR = &ADC1->DR
   - CMAR = &g_adc_value
   - CNDTR = 1
   - CCR：CIRC=1, PSIZE/MSIZE=01
6. 开启 ADC1 时钟
7. 配置 ADC 时钟分频（/6）
8. 配置 ADC 通道、采样时间
9. **打开 CONT + DMA**（连续转换 + DMA 请求）
10. 做 ADC 校准
11. **先开 DMA 通道**（CCR.EN=1）
12. **再启动 ADC 连续转换**（SWSTART）

**顺序不能乱**的原因：

- DMA 没准备好时，ADC 不应该先开始持续输出数据
- ADC 没打开 DMA 请求时，DMA 不会收到搬运触发

---

## 9. 关键的启动顺序：先 DMA 后 ADC

```c
DMA1_Channel1->CCR |= DMA_CCR_EN;  // 第 1 步：开 DMA
ADC1->CR2 |= EXTTRIG | SWSTART;    // 第 2 步：启动 ADC
```

如果顺序反了：

1. ADC 先启动 → 完成第一次转换 → 发出 DMA 请求
2. 但 DMA 通道还没使能（CCR.EN=0）→ DMA 忽略请求
3. 直到 DMA 使能后，ADC 的第二次转换才被处理
4. **第一次数据丢了**

所以标准做法是：先开 DMA，再开 ADC。

---

## 10. HAL 版核心变化

### 10.1 两个句柄

HAL 版需要两个句柄：

```c
static ADC_HandleTypeDef hadc1;      // ADC 句柄
static DMA_HandleTypeDef hdma_adc1;  // DMA 句柄
```

### 10.2 `__HAL_LINKDMA()` 的作用

这个宏很关键：

```c
__HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1);
```

它在做一件很容易忽略但非常关键的事：

**把 ADC 句柄和 DMA 句柄关联起来。**

这样后面当你调用：

```c
HAL_ADC_Start_DMA(&hadc1, &g_adc_value, 1)
```

HAL 才知道要配套使用哪一个 DMA 通道。

### 10.3 `HAL_ADC_Start_DMA()`

这是这节课最核心的 HAL API。

**内部操作：**

1. 通过 hadc1.DMA_Handle 找到配套的 DMA 句柄
2. 配置 DMA 的 CPAR = ADC1->DR 地址（自动）
3. 配置 DMA 的 CMAR = 传入的 &g_adc_value
4. 配置 DMA 的 CNDTR = 传入的 1
5. 使能 DMA 通道（CCR.EN=1）
6. 使能 ADC 的 DMA 请求（CR2.DMA=1）
7. 启动 ADC 转换（SWSTART）

**对应寄存器版的所有操作，全部封装在这一行里。**

---

## 11. 本课最容易卡住的地方

### 11.1 没开 DMA1 时钟

后果：DMA 配置写不进去，数据不会搬运。

### 11.2 ADC 没打开 DMA 功能

也就是没设 `ADC_CR2_DMA`（寄存器版）或没走 `HAL_ADC_Start_DMA`（HAL 版）。

后果：ADC 在转换，但 DMA 不会收到搬运请求。

### 11.3 数据宽度不匹配

ADC 结果是 12 位，按 16 位处理。PSIZE 和 MSIZE 必须一致。

### 11.4 DMA 没开循环模式

本课需要 `CIRC=1`，否则只搬一次 ADC 就停住了。

### 11.5 HAL 版忘了 `__HAL_LINKDMA()`

如果不关联，`HAL_ADC_Start_DMA()` 返回 HAL_ERROR。

### 11.6 启动顺序反了

先 ADC 后 DMA → 第一次数据丢失。

---

## 12. 本课你应该真正掌握什么

学完这节，不只是"会写一个 ADC + DMA demo"，而是应该真正明白：

1. **DMA 的本质是自动搬数据**，CPU 只需配置一次，DMA 永久工作
2. **ADC1 的结果先出现在 DR**，DMA 可以把 DR 的值自动搬到 RAM
3. **CPU 后续只需要读 RAM 里的结果**，不再需要碰 ADC 寄存器
4. **ADC + DMA 是非常经典的高频采样基础结构**
5. **DMA 需要 5 个关键信息**：源地址、目标地址、数量、宽度、方向
6. **启动顺序：先开 DMA，再开 ADC**

### 12.1 三条采样路线的对比

| 路线 | 核心代码 | CPU 负担 |
|------|---------|---------|
| 轮询采样 | `while(!EOC); val=DR;` | 全程被占用 |
| 中断采样 | 中断中读 DR | 只在中断时参与 |
| **DMA 采样** | **直接读内存变量** | **零参与** |

---

## 13. 下一课会学什么

下一课建议继续做 `13_adc_dma`。

因为这一课你看到了：

- DMA 把 ADC 数据搬到**一个变量**

下一课会升级到：

- DMA 把 ADC 数据搬到**一个数组**
- 然后对数组做**平均滤波处理**

这样你就能真正体验到：**采集 + 搬运 + 处理三步完全分离**的生产线模式。
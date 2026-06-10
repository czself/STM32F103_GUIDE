# STM32 课程分组审查流程

## 1. 用途

本文件用于约束后续分组检查课程时的固定流程。

每次检查一组课程前，先读本文件，再按同一套标准执行。目标不是只看能不能编译，而是同时检查：

- 目录结构是否完整
- `README.md` 是否和真实代码一致
- `reg/` 与 `hal/` 是否表达同一个实验目标
- 代码是否适合作为教学材料逐行阅读
- 是否存在能编译但上板会卡住的最小工程问题
- 是否存在会误导初学者的表述

## 2. 默认分组方式

默认按课程编号每 5 章一组检查。

示例：

- `00-05`：环境、LED、GPIO、时钟、内存映射、SysTick
- `06-10`：定时器基础、输出比较、PWM、EXTI
- `11-15`：输入捕获、PWM 输入、编码器、高级定时器、ADC 轮询

如果用户说“下五章”，以上一次检查结束编号为准继续向后看。

## 3. 每次检查先做结构检查

对本组每个课程目录确认这些文件存在：

```text
README.md
reg/src/main.c
hal/src/main.c
reg/platformio.ini
hal/platformio.ini
```

命令模板：

```bash
for d in 06_timer_base 07_timer_output_compare 08_pwm_basic 09_pwm_advanced 10_exti; do
  echo "$d"
  for p in README.md reg/src/main.c hal/src/main.c reg/platformio.ini hal/platformio.ini; do
    test -f "$d/$p" || echo "  MISSING $p"
  done
done
```

如果课程有额外工程，例如 `09_pwm_advanced/pwm_to_din_test`，也要纳入编译检查。

## 4. 检查代码可读性

先看文件行数和长行数量，快速判断是否存在压缩写法：

```bash
for f in 06_timer_base/{reg,hal}/src/main.c 07_timer_output_compare/{reg,hal}/src/main.c; do
  lines=$(wc -l < "$f")
  long=$(awk 'length($0)>160{c++} END{print c+0}' "$f")
  printf '%4d lines %2d long %s\n' "$lines" "$long" "$f"
done
```

重点看：

- 是否一行塞多个关键动作
- `main()`、中断函数、回调函数是否被压成一行
- 寄存器操作前是否解释寄存器、bit、顺序和硬件后果
- HAL 结构体字段是否能反推到底层寄存器意图
- 注释是否解释“为什么”，而不是只复述代码

## 5. 编译验证

本组所有 `reg/` 和 `hal/` 工程都要跑 `pio run`。

命令模板：

```bash
PIO=/home/sz/.platformio/penv/bin/pio
for d in 06_timer_base/{reg,hal} 07_timer_output_compare/{reg,hal}; do
  printf "BUILD %s\n" "$d"
  (cd "$d" && "$PIO" run -s)
  rc=$?
  printf "RESULT %s %s\n" "$d" "$rc"
  if [ "$rc" -ne 0 ]; then exit "$rc"; fi
done
```

如果 `/home/sz/.platformio/penv/bin/pio` 不存在，再检查：

```bash
command -v pio
test -x /home/sz/.platformio-venv/bin/pio && echo /home/sz/.platformio-venv/bin/pio
```

## 6. HAL 最小工程必查项

凡是 HAL 工程调用了 `HAL_Init()`，都要检查是否需要提供：

```c
void SysTick_Handler(void)
{
    HAL_IncTick();
}
```

如果代码使用了 `HAL_Delay()`，这个入口必须存在。

即使本课没有使用 `HAL_Delay()`，在当前最小 PlatformIO HAL 工程里也建议保留该入口，避免 `HAL_Init()` 配置 SysTick 后落到启动文件默认中断处理函数。

## 7. 外设专项检查

### 7.1 GPIO

检查点：

- 使用 GPIO 前是否打开对应 GPIOx 时钟
- F103 的输入上拉/下拉是否同时处理 `CNF` 和 `ODR`
- BluePill PC13 是否按低电平点亮讲清楚
- HAL 版 `GPIO_PULLUP` 是否映射回寄存器版解释

### 7.2 时钟树

检查点：

- HSE 8MHz、PLL x9、SYSCLK 72MHz 是否一致
- Flash latency 是否在 72MHz 前配置
- APB1 是否分到 36MHz
- README 是否说明没有 HSE 时会卡在 `HSERDY` 或进入错误处理

### 7.3 SysTick / HAL Tick

检查点：

- `SysTick->LOAD` 是否和 HCLK 计算一致
- `SysTick_Handler()` 名字是否正确
- 自定义 tick 和 HAL tick 是否分清楚
- `HAL_Delay()` 卡住的排查链是否讲清楚

### 7.4 TIM / PWM

检查点：

- APB1 定时器时钟是否按 72MHz 计算清楚
- `PSC`、`ARR`、`CCR` 是否能算出周期和占空比
- 输出比较是否讲清 `CNT == CCRx` 后的硬件动作
- PWM 是否讲清 `ARR + 1` 是周期计数数，`CCR` 接近/达到 `ARR + 1` 是满占空比边界
- HAL 版是否调用了 `HAL_TIM_*_Start()`，不是只 Init/Config

### 7.5 EXTI

检查点：

- GPIO 输入、AFIO 映射、EXTI 触发、NVIC 放行四道门是否都讲清楚
- F1 HAL 版 EXTI 是否显式打开 AFIO 时钟
- `EXTI->PR` 是否写 1 清 pending
- HAL 版 IRQHandler 是否调用 `HAL_GPIO_EXTI_IRQHandler()`
- README 是否说明机械按键抖动可能导致一次按下多次触发

### 7.6 ADC

检查点：

- STM32F1 HAL ADC 是否显式调用 `HAL_ADCEx_Calibration_Start()`
- README 中寄存器版校准位 `CAL` 和 HAL 版校准 API 是否能互相对应
- ADC 时钟分频是否符合 F1 ADC 时钟限制

### 7.7 FreeRTOS

检查点：

- `FreeRTOSConfig.h` include 路径是否指向正确配置
- `INCLUDE_vTaskSuspend` 等 API 配置是否和代码使用一致
- 任务、队列、信号量、事件组、通知、软件定时器是否说明阻塞/唤醒关系
- FromISR API 是否只在 ISR 中使用，并处理 `portYIELD_FROM_ISR`
- malloc failed / stack overflow hook 是否有教学解释

## 8. README 与代码一致性检查

每次至少抽查这些内容：

- README 中函数名是否真实存在
- README 中运行现象是否 reg/hal 都成立
- README 中的引脚、通道、外设编号是否和代码一致
- README 中的“下一课”链接是否和实际课程编号一致
- README 中是否把附加实验和主线实验边界讲清楚
- 代码注释和 README 是否互相矛盾

常用搜索：

```bash
rg -n "TODO|FIXME|占位|预览|当前源码|未严格|app_init|01_gpio|HAL_Delay|SysTick_Handler|HAL_IncTick" 06_timer_base 07_timer_output_compare
```

## 9. 输出结论格式

每次检查后，先给验证结果，再给问题。

建议格式：

```text
我检查了 06-10：

- 结构完整：...
- 编译结果：...
- 需要修的问题：
  1. ...
  2. ...
- 可以接受但要知道的边界：
  - ...
```

只报告真正值得改的点。不要把已经解释清楚的教学取舍当成错误。

## 10. 修复原则

- 小问题直接修，不大范围重写。
- 不覆盖用户已有改动。
- 修代码后跑本组全部相关工程。
- 修 README 后至少重新检查对应行附近的表述。
- 如果修的是共性问题，例如 HAL tick、ADC 校准、FreeRTOS 配置，要顺手搜索同类课程是否也存在。

# 55_freertos_task_notification - FreeRTOS 任务通知

## 1. 本课到底在学什么

本课表面现象是：PC13 大约每 500ms 翻转一次。这个节奏不是 led 任务自己延时得到的，而是 sender 任务每 500ms 给 led 任务发送一次任务通知，led 任务收到通知后才翻转 PC13。

真正要学的是 FreeRTOS task notification，任务通知。它是直接嵌在任务控制块 TCB 里的轻量同步机制，不需要额外创建队列、信号量或事件组对象。本课用 `xTaskNotifyGive(g_led_task)` 发送通知，用 `ulTaskNotifyTake(pdTRUE, portMAX_DELAY)` 等待并消费通知。

这节课接在事件组之后。队列能传数据，信号量能表达许可，事件组能组合 bit，任务通知则适合“一对一通知某个任务”。后面做串口、DMA 或轻量事件唤醒时，任务通知通常比队列/信号量更省内存、更快。

## 2. 本课学习目标

学完本课你应该能做到：

- 能解释 PC13 为什么跟着 sender 的 500ms 通知节奏翻转。
- 能说明 `TaskHandle_t g_led_task` 为什么必须保存 led 任务句柄。
- 能解释 `xTaskNotifyGive(g_led_task)` 给的是哪个任务。
- 能解释 `ulTaskNotifyTake(pdTRUE, portMAX_DELAY)` 的两个参数。
- 能说明任务通知值和计数信号量有什么相似点。
- 能说明任务通知为什么是一对一，不适合多个任务广播。
- 能区分任务通知、队列、二值信号量和事件组的适用场景。
- 能把寄存器版和 HAL 版的 PC13 翻转对应起来。
- 能根据 PC13 不闪、只闪一次、通知丢失、句柄为 NULL 等现象排查。

## 3. 本课目录结构

```text
55_freertos_task_notification/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

两个版本都只需要 `FreeRTOS.h` 和 `task.h`，没有 `queue.h`、`semphr.h`、`event_groups.h`。这正是任务通知的特点：通知状态存在目标任务自己的 TCB 中，不需要额外内核对象句柄。

## 4. 实验硬件

- STM32F103C8T6 BluePill。
- ST-Link 下载器。
- PC13：led_task 收到通知后翻转。
- PA1、PA2：初始化为输出，但当前源码没有使用。
- PA0：初始化为上拉输入，但当前源码没有读取。
- 系统时钟：HSE 8MHz，PLL x9 到 72MHz。
- FreeRTOS tick：`configTICK_RATE_HZ = 1000`，所以 `pdMS_TO_TICKS(500)` 约为 500 tick。
- 关键配置：`INCLUDE_xTaskGetCurrentTaskHandle = 1` 等任务 API 已启用，任务通知相关 API 在 task 模块中。

本课没有中断通知，也没有 FromISR 任务通知 API。全部通知都发生在普通任务上下文。

## 5. 先建立一个最基本的脑图

```text
复位启动
  -> 配 72MHz 时钟
  -> 配 PC13/PA1/PA2 输出，PA0 上拉输入
  -> 创建 led_task，优先级 2，句柄写入 g_led_task
  -> 创建 sender，优先级 1
  -> 启动调度器
  -> led_task 先运行，阻塞在 ulTaskNotifyTake()
  -> sender 每 500ms 调用 xTaskNotifyGive(g_led_task)
  -> led_task 被通知唤醒
  -> led_task 消费通知计数，翻转 PC13
  -> led_task 再次阻塞等待下一次通知
```

这条链路里，`g_led_task` 是关键连接点。sender 不是给“某个函数”发通知，而是给 led 任务的 TCB 发通知。若 led 任务句柄没有保存，sender 就不知道通知目标。

从现象层看 PC13 每 500ms 翻转；从 RTOS 层看是目标任务的通知值被加 1、等待任务被唤醒、通知值被清零或递减；从硬件层看只是最终执行了一次 GPIO 翻转。

## 6. 先认识本课里出现的核心名词

### 6.1 `Task Notification` 是什么

Task Notification 是任务通知，属于 FreeRTOS 任务内建同步机制。

每个任务 TCB 中都有通知状态和通知值。其他任务可以直接通知某个任务，目标任务可以阻塞等待自己的通知。它比队列、信号量少一个额外内核对象。

本课需要它，是为了演示轻量的一对一任务唤醒。

### 6.2 `TaskHandle_t g_led_task` 是什么

`g_led_task` 是 led 任务句柄。

main 创建 `led_task` 时把最后一个参数写成 `&g_led_task`，内核把 led 任务句柄保存进去。sender 后续用这个句柄调用 `xTaskNotifyGive()`。

若 `g_led_task` 为 NULL，通知目标无效，PC13 不会按预期翻转。

### 6.3 `xTaskNotifyGive()` 是什么

`xTaskNotifyGive()` 是任务通知的 give 形式 API。

源码中 sender 每 500ms 调用它，给 `g_led_task` 的通知值加 1，并在 led_task 正等待通知时唤醒它。它类似“给目标任务一个计数许可”。

如果目标任务句柄错误，会通知错误对象或导致异常。

### 6.4 `ulTaskNotifyTake()` 是什么

`ulTaskNotifyTake()` 是等待并获取通知值的 API。

led_task 调用 `ulTaskNotifyTake(pdTRUE, portMAX_DELAY)`。没有通知时阻塞，有通知时返回通知计数，并根据第一个参数决定清零还是递减。

本课没有检查返回值，因为使用无限等待，返回就表示收到通知。

### 6.5 `pdTRUE` 在 `ulTaskNotifyTake()` 里是什么

第一个参数为 `pdTRUE`，表示退出时把通知值清零。

若通知值累计了多次，led_task 一次 take 后会清空。若用 `pdFALSE`，则每次只减一，更像计数信号量逐个消费。

本课 500ms 通知一次、led_task 处理很快，通常不会积压。

### 6.6 `portMAX_DELAY` 是什么

第二个参数 `portMAX_DELAY` 表示 led_task 一直等待通知。

等待期间 led_task 阻塞，不占 CPU。sender 发通知后，内核唤醒 led_task。

### 6.7 `sender` 是什么

sender 是通知发送任务。

它优先级 1，循环中先延时 500ms，再给 led_task 发通知。它决定 PC13 的节奏。

### 6.8 `led_task` 是什么

led_task 是通知接收任务。

它优先级 2，高于 sender。收到通知后能及时运行并翻转 PC13，然后再次等待。

### 6.9 `通知值` 是什么

通知值是目标任务 TCB 中的一个整数状态。

`xTaskNotifyGive()` 会增加这个值，`ulTaskNotifyTake()` 会读取并清零或减一。它不是队列消息，不保存多个不同结构体数据。

### 6.10 任务通知和队列有什么区别

队列是独立对象，可以缓存多个固定大小数据元素。

任务通知在目标任务 TCB 内，适合轻量事件/计数，不适合传复杂数据或多个消费者共享。

### 6.11 任务通知和二值信号量有什么区别

二值信号量是独立对象，任务通知是目标任务内建字段。

二者都能唤醒任务，但任务通知更轻；二值信号量更适合对象化共享或多个任务围绕同一个同步对象工作。

### 6.12 任务通知和事件组有什么区别

事件组用 bit 表示多个条件，可被多个任务设置/等待。

任务通知默认是通知某一个具体任务，适合一对一，不适合广播多个任务。

### 6.13 `PC13` 在本课做什么

PC13 是通知被消费后的现象证据。

寄存器版用 `led_toggle_pc13()`，HAL 版用 `HAL_GPIO_TogglePin()`。它不参与通知机制，只显示 led_task 被唤醒。

### 6.14 `heap` 和任务通知有什么关系

任务通知本身不需要额外创建对象，但任务仍要从 heap 分配 TCB 和栈。

如果 `xTaskCreate()` 失败，通知链路无法建立。malloc failed hook 和 stack overflow hook 仍然重要。

### 6.15 FromISR 任务通知是什么

FreeRTOS 也有中断上下文任务通知 API，例如 `vTaskNotifyGiveFromISR()`。

本课没有使用 ISR 通知。若未来在中断里唤醒任务，应使用 FromISR 版本并处理 yield。

## 7. 寄存器版代码逐步讲解

### 7.1 头文件分工

寄存器版只包含 `stm32f1xx.h`、`FreeRTOS.h`、`task.h`。

任务通知 API 属于 task 模块，不需要 queue/semaphore/event group 头文件。

### 7.2 时钟初始化

系统配置到 72MHz，保证 FreeRTOS tick 和 `pdMS_TO_TICKS(500)` 对应关系正确。

时钟不准时，通知节奏会偏。

### 7.3 GPIO 初始化

GPIOC/ GPIOA 打开时钟，PC13 输出，PA1/PA2 输出，PA0 上拉输入。

本课只使用 PC13。

### 7.4 创建 led_task 并保存句柄

`xTaskCreate(led_task, "led", 128, NULL, 2, &g_led_task)`。

最后一个参数非常关键，sender 后续要用 `g_led_task` 作为通知目标。

### 7.5 创建 sender

`xTaskCreate(sender, "send", 128, NULL, 1, NULL)`。

sender 不需要被别的任务控制，所以不保存句柄。

### 7.6 led_task 的等待语句

`ulTaskNotifyTake(pdTRUE, portMAX_DELAY)` 让 led_task 阻塞等待通知。

收到通知后返回，led_task 翻转 PC13。

### 7.7 sender 的发送语句

sender 先延时 500ms，再 `xTaskNotifyGive(g_led_task)`。

这让 led_task 大约每 500ms 被唤醒一次。

### 7.8 为什么 led_task 优先级更高

led_task 优先级 2，sender 优先级 1。

通知到来后，led_task 能更快运行并翻转 PC13。sender 发完通知后继续下一轮延时。

### 7.9 创建失败检查

main 用 `ok` 汇总两个任务创建结果。失败就关中断停住。

若 led_task 创建失败，`g_led_task` 无效；若 sender 创建失败，没有通知来源。

### 7.10 当前没有额外同步对象

本课没有队列、信号量、事件组或软件定时器。

这正是任务通知的轻量性：同步状态直接在任务 TCB 中。

## 8. HAL 版代码逐步讲解

### 8.1 HAL 初始化

HAL 版先 `HAL_Init()`，再配置时钟和 GPIO。

任务通知逻辑不依赖 HAL。

### 8.2 HAL GPIO 配置

PC13 配为推挽输出并初始置高。PA1/PA2 输出、PA0 上拉输入。

本课 HAL 版只使用 PC13 作为通知现象。

### 8.3 HAL 版 led_task

led_task 等待任务通知，收到后调用 `HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13)`。

等待和唤醒逻辑与寄存器版相同。

### 8.4 HAL 版 sender

sender 每 500ms 调用 `xTaskNotifyGive(g_led_task)`。

这不是 HAL API，而是 FreeRTOS task API。

### 8.5 HAL 版也必须保存句柄

HAL 版创建 led_task 时同样传入 `&g_led_task`。

没有句柄就无法指定通知目标。

### 8.6 HAL 版不使用 HAL_Delay

sender 使用 `vTaskDelay()`，led_task 使用任务通知阻塞。

RTOS 等待应由 FreeRTOS 管理。

### 8.7 HAL 版调试方法

在 `xTaskNotifyGive()` 和 `ulTaskNotifyTake()` 后打断点，可以验证通知是否送达。

若通知到达但 PC13 不动，再查 HAL GPIO。

### 8.8 HAL 版没有回调

本课没有 HAL 中断回调。不要把任务通知误解成 HAL callback。

## 9. 两个版本真正应该怎么学

reg/hal 的差异只在 PC13 翻转方式。任务通知机制完全相同：sender 通过任务句柄通知 led_task，led_task 阻塞等待并消费通知。

学习重点是“通知目标是任务本身”。队列有队列句柄，信号量有信号量句柄，事件组有事件组句柄；任务通知没有额外对象，它依赖目标任务句柄。

## 10. 检验问题清单

### 10.1 为什么必须保存 `g_led_task`？

**答**：sender 需要用它指定通知目标。没有句柄就不知道通知哪个任务。

### 10.2 `xTaskNotifyGive()` 做了什么？

**答**：给目标任务的通知值加一，并在目标任务等待时唤醒它。

### 10.3 `ulTaskNotifyTake(pdTRUE, ...)` 的 `pdTRUE` 表示什么？

**答**：返回时把通知值清零。

### 10.4 `portMAX_DELAY` 表示什么？

**答**：led_task 一直等到通知到来，等待期间阻塞不占 CPU。

### 10.5 PC13 翻转说明什么？

**答**：说明 led_task 已收到并消费了一次任务通知。

### 10.6 任务通知适合广播多个任务吗？

**答**：不适合。它主要是一对一通知具体任务，广播条件更适合事件组。

### 10.7 本课有没有队列或信号量？

**答**：没有。同步完全靠任务通知。

### 10.8 通知会传复杂数据吗？

**答**：本课使用 give/take 计数形式，不传复杂数据。复杂数据应使用队列或其他通知模式配合额外存储。

### 10.9 HAL 版任务通知 API 是否不同？

**答**：不不同，仍是 FreeRTOS 原生 API。

### 10.10 如果 sender 没创建成功会怎样？

**答**：没有通知来源，led_task 会一直阻塞，PC13 不翻。

## 11. 工程实现步骤

### 11.1 需求分析

用任务通知实现 sender 到 led_task 的轻量一对一唤醒。

### 11.2 硬件核查

确认 PC13 可观察，PA0 不参与，ST-Link 可下载。

### 11.3 寄存器路线

配置时钟和 GPIO，创建 led_task 保存句柄，创建 sender，sender 通知 led_task，led_task 翻 PC13。

### 11.4 HAL 路线

用 HAL 初始化硬件，任务通知逻辑保持不变，PC13 用 HAL 翻转。

### 11.5 工程思维

一对一轻量事件优先考虑任务通知；需要缓存数据、广播或多消费者时，再考虑队列/事件组。

### 11.6 常见工程陷阱

忘记保存目标任务句柄、目标任务还没创建就通知、误用任务通知做广播、不理解 `pdTRUE` 清零语义、不检查任务创建失败，都是常见问题。

## 12. 运行现象

PC13 大约每 500ms 翻转一次。sender 每 500ms 发一次通知，led_task 收到后翻转 PC13。

调试器中可以观察 `g_led_task` 是否非 NULL，也可以在 `ulTaskNotifyTake()` 返回后打断点。

### 12.1 六层对应关系再核对

现象层：PC13 周期翻转。你看到的是 LED，不是通知本身。

硬件层：PC13 由 GPIOC 输出寄存器控制，通知机制不直接碰 GPIO。

芯片模块层：GPIOC 负责输出电平，Cortex-M 和 FreeRTOS 调度负责让任务在合适时刻运行。

寄存器层：寄存器版最终写 `GPIOC->BSRR` 或 `GPIOC->BRR`，这些写入才让电平变化。

C/CMSIS 层：`led_toggle_pc13()` 是现象落地函数，`sender()` 和 `led_task()` 是 RTOS 逻辑函数。

RTOS 层：通知值存在 led_task 的 TCB 里，`xTaskNotifyGive()` 增加通知值，`ulTaskNotifyTake()` 等待并消费通知值。

HAL/工程层：HAL 版只把 PC13 翻转换成 `HAL_GPIO_TogglePin()`，任务通知 API 和句柄关系完全不变。

### 12.2 本课推荐断点

第一个断点放在 `xTaskCreate(led_task, ..., &g_led_task)` 之后，看 `g_led_task` 是否非 NULL。

第二个断点放在 sender 的 `xTaskNotifyGive(g_led_task)`，确认通知发送周期来自 sender 的 500ms 延时。

第三个断点放在 led_task 的 `ulTaskNotifyTake()` 返回之后，确认 LED 翻转不是 led_task 自己定时，而是收到通知后发生。

第四个断点放在 `led_toggle_pc13()` 或 `HAL_GPIO_TogglePin()`，确认 RTOS 层已经走到硬件输出层。

### 12.3 和队列/信号量/事件组的边界

队列适合保存数据，例如 UART 字节、结构体消息、采样值。任务通知的 give/take 形式只表达计数通知，不适合承载多个不同数据元素。

二值信号量适合把一个同步对象公开给多个任务或 ISR 使用。任务通知直接绑定目标任务，省对象、省内存，但共享性弱。

事件组适合多个 bit 条件组合，例如等待 bit0 和 bit2 同时满足。任务通知默认唤醒一个具体任务，不能自然表达多个任务广播。

所以本课出现任务通知，不是为了替代所有同步机制，而是为了演示最轻的一对一唤醒路径。

### 12.4 源码逐项核对

`g_led_task` 只应该由创建 led_task 的 `xTaskCreate()` 写入。sender 不应该自己构造句柄，也不应该在句柄为 NULL 时发送通知。

`led_task` 的优先级是 2，sender 的优先级是 1。这个安排让通知到达后，接收任务可以比发送任务更快处理 LED 翻转。

`ulTaskNotifyTake(pdTRUE, portMAX_DELAY)` 的返回值虽然本课没有保存，但调试时可以临时保存它。若返回值大于 1，说明通知曾经积压；若一直不返回，说明发送链路没有走通。

`portMAX_DELAY` 让 led_task 不需要轮询标志位。和裸机 while 轮询相比，这才是 RTOS 同步机制的价值：任务没事时睡着，有事时被唤醒。

### 12.5 为什么本课没有额外对象创建

前面队列、信号量、事件组都会先创建一个内核对象，再让任务围绕这个对象等待或发送。

任务通知不同，等待状态已经在每个任务 TCB 里，所以本课没有 `xQueueCreate()`、`xSemaphoreCreate...()` 或 `xEventGroupCreate()`。

这也是它省内存的原因：少一个对象，就少一份对象控制块和可能的缓冲区。代价是它更绑定目标任务，不像队列那样天然适合多个生产者和消费者共享数据。

## 13. 常见问题排查

### 13.1 PC13 完全不闪

查 led_task 和 sender 是否创建成功，`g_led_task` 是否非 NULL，是否进入 hook，再查 GPIO。

### 13.2 sender 在跑但 led_task 不醒

检查 `xTaskNotifyGive(g_led_task)` 的目标句柄是否正确，led_task 是否阻塞在 `ulTaskNotifyTake()`。

### 13.3 只闪一次

检查 sender 是否仍在循环，`vTaskDelay(500ms)` 后是否继续执行通知。

### 13.4 节奏不是 500ms

检查系统时钟、`configTICK_RATE_HZ` 和 `pdMS_TO_TICKS(500)`。

### 13.5 通知积压行为不符合预期

确认 `ulTaskNotifyTake()` 第一个参数是 `pdTRUE` 还是 `pdFALSE`。清零和递减会导致不同消费行为。

### 13.6 HAL 版通知到了但 LED 不动

查 `HAL_GPIO_Init()` 和 `HAL_GPIO_TogglePin()`，确认 PC13 硬件接法。

### 13.7 进入 malloc failed hook

任务创建需要 heap。虽然通知不额外创建对象，但任务 TCB 和栈仍需分配。

### 13.8 想从 ISR 通知任务

本课不是 ISR 场景。应改用 FromISR 任务通知 API，并处理是否需要 yield。

### 13.9 led_task 句柄一直是 NULL

先确认 `xTaskCreate(led_task, "led", 128, NULL, 2, &g_led_task)` 的最后一个参数确实是 `&g_led_task`。

如果误写成 `NULL`，任务本身可能创建成功，但句柄不会保存，sender 没有合法目标。

### 13.10 通知发送太早

本课在两个任务都创建完成后才启动调度器，sender 不会在 led_task 创建前运行。

如果你改成运行中动态创建任务，就必须保证目标任务句柄有效后再通知。

### 13.11 把任务通知当成队列使用

`xTaskNotifyGive()` 只增加目标任务通知值，不会保存每个字节、结构体或不同消息类型。

若你需要保存多个不同数据元素，应该回到队列；若只需要唤醒一个任务，任务通知才合适。

### 13.12 `pdTRUE` 和 `pdFALSE` 现象不同

`pdTRUE` 会在 take 成功后清零通知值，适合“只关心来了没有”的场景。

`pdFALSE` 每次只减一，更像逐个消费计数许可。若 sender 发送很快，这两种写法会导致 LED 补闪行为不同。

## 14. 本课最核心的结论

1. 任务通知是任务 TCB 内建的轻量同步机制。
2. `xTaskNotifyGive()` 通知指定任务，必须有目标任务句柄。
3. `ulTaskNotifyTake()` 让任务阻塞等待通知。
4. `pdTRUE` 表示 take 后清零通知值。
5. 任务通知适合一对一轻量事件，不适合广播或复杂数据缓存。
6. PC13 翻转证明 led_task 消费了一次通知。
7. reg/hal 差异只在 GPIO，通知机制相同。
8. 通知不额外创建对象，但任务创建仍消耗 heap 和 stack。

## 15. 建议你现在怎么读这节课

先记住三个点：目标任务句柄、发送通知、接收通知。然后看第 5 章链路，把 PC13 翻转对应到 led_task 被唤醒。最后再和队列、信号量、事件组比较适用场景。

## 16. 扩展练习

1. 把 sender 周期改成 1000ms，观察 PC13 变慢。
2. 把 `pdTRUE` 改成 `pdFALSE`，思考通知积压消费差异。
3. 增加第二个接收任务，验证任务通知不是广播。
4. 用 FromISR API 从中断通知 led_task。
5. 记录 `ulTaskNotifyTake()` 返回值，观察是否有积压。

## 17. 下一课预告

- 上一课：[54_freertos_event_group](../54_freertos_event_group/README.md)
- 下一课：[56_freertos_software_timer](../56_freertos_software_timer/README.md)

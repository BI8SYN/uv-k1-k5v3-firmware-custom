# K 头 Q_OUT：有效接收指示输出

日期：2026-09-20
状态：设计已确认，待实现

## 目标

把 UV-K1 / UV-K5 V3 的 K 头改造成标准模拟热点接口。手台收到有效信号时，通过
K 头 2.5mm 插头的 Ring（MCU 的 PA9）对外输出一个低电平有效的静噪指示信号
（下称 Q_OUT），供外部设备判断"现在有真实的 RF 接收"。

完整的接口设想如下，本 spec **只实现 Q_OUT 一项**：

```
K6 V3                     外部设备
──────────────────────────────────
SPK OUT  ───────────────→ RX AUDIO     （原机硬件，不改）
MIC IN   ←─────────────── TX AUDIO     （原机硬件，不改）
PTT IN   ←─────────────── 拉低触发发射  （原机硬件，不改）
Q OUT    ───────────────→ 有效接收指示  ← 本 spec
GND      ──────────────── GND
```

引脚映射：

```
2.5mm Tip     = SPK OUT
2.5mm Ring    = Q OUT      ← 本 spec 修改
2.5mm Sleeve  = GND
3.5mm Ring    = MIC IN
3.5mm Sleeve  = PTT IN
```

音频电平、隔直、阻抗和隔离属于后续阶段，不在本 spec 范围内。

## 非目标

- 不碰任何音频通路代码。
- 不碰 PTT 代码。机台 PTT 是 PB10（`App/driver/gpio.h:31`），与本改动无关。
- 不新增菜单项、不新增运行时开关、不新增屏幕指示。
- 不改 `CheckRadioInterrupts()` 的 10 ms 轮询调度。

## 行为契约

Q_OUT 严格跟随 `gCurrentFunction == FUNCTION_RECEIVE`。

| 状态 | PA9 电平 |
|---|---|
| 开机、待机、`FUNCTION_FOREGROUND` | 高（内部弱上拉，≈3.3V） |
| `FUNCTION_INCOMING`（静噪开但亚音未验证） | 高 |
| `FUNCTION_RECEIVE`（有效接收，音频已开） | **低（硬拉 0V）** |
| `FUNCTION_MONITOR`（用户手动监听） | 高 |
| `FUNCTION_TRANSMIT` | 高 |
| `FUNCTION_POWER_SAVE` | 高 |

**为什么是 `FUNCTION_RECEIVE` 而不是 `FUNCTION_IsRx()`**：后者包含
`FUNCTION_MONITOR`。用户按监听键强开静噪时不存在真实 RF 接收，不应误报。

**为什么不是 `FUNCTION_INCOMING`**：`INCOMING` 表示静噪已开但 CTCSS/DCS 尚未
验证通过。注意 `App/functions.h` 里 `FUNCTION_RECEIVE` 和 `FUNCTION_INCOMING`
的注释是反的，以 `App/app/app.c` 的实际状态机为准：
`HandleIncoming()` 验证通过后才调用
`APP_StartListening(gMonitor ? FUNCTION_MONITOR : FUNCTION_RECEIVE)`
（`App/app/app.c:526`）。

**电气特性**：开漏输出 + MCU 内部上拉。外部可自行上拉到 3.3V 或 5V，也可直接
驱动三极管、MOS 或模拟开关。选择开内部上拉（而非真高阻）是为了让第一阶段能用
万用表直接量出 3.3V / 0V 的干净跳变；该弱上拉约 30–50 kΩ，会被任何外部上拉或
负载轻松压过，不影响接外设。

## 架构决策

### 决策 1：与 UART 编译期互斥，`ENABLE_UART` 整个关掉

PA9 当前是 USART1_TX（`App/driver/uart.c:56`，AF1）。二者不能共存。

选择"关掉整个 `ENABLE_UART`"而非"保留 UART 但 PA9 不配 TX"，因为后者会产生一个
能编译、能运行、但 CHIRP/UV Studio 握手必然失败（它们需要回包）的半残状态。

代价可接受，因为本仓库 `ENABLE_USB` 默认开启（CherryUSB + CDC，
`App/CMakeLists.txt:102`），上层协议 `App/app/uart.c` 由 USB 和 UART 共用。
现有代码已经完整支持 `ENABLE_UART=OFF` + `ENABLE_USB=ON` 的组合：

- `App/main.c:55,73,94` 的 `UART_Init()` / `UART_Send()` 有 `#ifdef` 守卫
- `App/k5viewer.c:96-108` 有 `#elif defined(ENABLE_USB)` 分支
- `App/app/uart.c:282` 的回包路径有守卫
- `App/debugging.h:4` 整个文件体被 `#ifdef ENABLE_UART` 包住
- `App/CMakeLists.txt:182` 只在 UART **和** USB 都关时才禁用 K5Viewer

因此 CHIRP、UV Studio、K5Viewer 会自动退到 USB-C，功能不丢。

### 决策 2：只加编译开关，不新增构建目标

新增 `ENABLE_KHEAD_Q_OUT`，在 default preset 中置为 `false`。不新建 preset，
不改 `compile-firmware.sh`。现有 6 个发布目标行为零变化。

构建命令（脚本已支持透传 CMake 变量）：

```
./compile-firmware.sh Fusion -DENABLE_UART=OFF -DENABLE_KHEAD_Q_OUT=ON
```

理由：diff 最小，与上游 armel/uv-k1-k5v3-firmware-custom 合并时冲突面最小。

命名不带 `FEAT_F4HWN_` 前缀，因为这不是 F4HWN 上游的功能，不应冒用其命名空间。

### 决策 3：钩子放在 `FUNCTION_Select()`，且只放一处

`gCurrentFunction` 全仓库只在 `App/functions.c:229` 被赋值一次。紧贴该赋值下一行
挂钩，可覆盖全部状态迁移，包括 TX、省电、扫描。

不能放在 `FUNCTION_Select()` 的后半段：该函数对 `FOREGROUND` 和 `POWER_SAVE`
会提前 `return`。

不在 `APP_StartListening()` 里额外提前断言。`FUNCTION_Select(function)` 是该函数
的最后一句，在它之前有 `AUDIO_AudioPathOn()` 和约 16 次位翻转 SPI 寄存器写
（`BK4819_SetRxAudioGain()` + `RADIO_SetModulation()`）。按 48 MHz 主频、
每 bit 三次 40-nop 延时计算，每次寄存器写约 63 µs，合计约 1 ms。

该 1 ms 在整条延迟链中是最小项（`CheckRadioInterrupts()` 挂在 10 ms 时间片上，
开亚音时 `INCOMING→RECEIVE` 还要等几十到几百 ms 的解码确认），不值得为它引入
第二个策略点。已确认接受。

## 实现改动

全部改动包在 `#ifdef ENABLE_KHEAD_Q_OUT` 内。

### 1. `App/driver/gpio.h`

在 `GPIO_PINS` 枚举中加入：

```c
GPIO_PIN_Q_OUT = GPIO_MAKE_PIN(GPIOA, LL_GPIO_PIN_9),
```

并在现有 `GPIO_EnableAudioPath()` 等内联包装旁边加入：

```c
static inline void GPIO_SetQOut(bool active)
{
    // Active low: assert pulls the line to GND, release lets the
    // pull-up (internal or external) take it high.
    if (active)
        GPIO_ResetOutputPin(GPIO_PIN_Q_OUT);
    else
        GPIO_SetOutputPin(GPIO_PIN_Q_OUT);
}
```

### 2. `App/board.c` / `BOARD_GPIO_Init()`

在函数末尾追加一个独立的 `do { } while (0)` 块，使用自己的
`LL_GPIO_InitTypeDef`，不污染函数中复用的那个结构体。

- 先 `LL_GPIO_SetOutputPin(GPIOA, LL_GPIO_PIN_9)` 置为不有效，再 `LL_GPIO_Init`，
  避免上电瞬间打出一个假的低脉冲。这与现有 LCD A0 / CS 的写法一致
  （`App/board.c:98-99`）。
- PA9：`Mode = LL_GPIO_MODE_OUTPUT`，`OutputType = LL_GPIO_OUTPUT_OPENDRAIN`，
  `Pull = LL_GPIO_PULL_UP`。
- PA10 一并泊住：`ENABLE_UART=OFF` 后不再有代码初始化它，它会停在复位默认的
  浮空输入态，而该引脚引到 K 头外部连接器。配成
  `Mode = LL_GPIO_MODE_INPUT`，`Pull = LL_GPIO_PULL_UP`（等同 UART 空闲电平）。

### 3. `App/functions.c` / `FUNCTION_Select()`

紧接 `gCurrentFunction = Function;`（第 229 行）之后：

```c
#ifdef ENABLE_KHEAD_Q_OUT
    GPIO_SetQOut(Function == FUNCTION_RECEIVE);
#endif
```

该文件已 `#include "driver/gpio.h"`，无需新增包含。

### 4. `App/CMakeLists.txt`

```cmake
enable_feature(ENABLE_KHEAD_Q_OUT)

if(ENABLE_KHEAD_Q_OUT AND ENABLE_UART)
    message(FATAL_ERROR
        "ENABLE_KHEAD_Q_OUT needs PA9, which USART1_TX also uses. "
        "Build with -DENABLE_UART=OFF.")
endif()
```

这条硬门禁不是洁癖。`UART_Init()` 在 `BOARD_Init()` **之后**执行
（`App/main.c:95`），两个都开时 UART 会把 PA9 悄悄重配为 AF1，固件能编译、能运行、
Q_OUT 就是不动 —— 这种失败极难定位。必须在构建期失败。

### 5. `CMakePresets.json`

default preset 加 `"ENABLE_KHEAD_Q_OUT": false`。

### 6. `README.md`

补充编译选项说明、上表的接线图、构建命令，以及下节的已知限制。

## 验证

1. `./compile-firmware.sh Fusion -DENABLE_UART=OFF -DENABLE_KHEAD_Q_OUT=ON`
   构建通过，且不突破脚本自带的 118 KiB Flash / 16 KiB RAM 上限。
2. `./compile-firmware.sh All` 全部 5 个发布目标构建通过 —— 证明默认关闭时本改动
   确实是惰性的。
3. 反向门禁：`./compile-firmware.sh Fusion -DENABLE_KHEAD_Q_OUT=ON`（不关 UART）
   必须以 `FATAL_ERROR` 失败。
4. 台架实测，万用表打在 2.5mm Ring 对 Sleeve：
   - 待机 → 约 3.3V
   - 另一台手台发射 → 0V
   - 对方松开 → 回到约 3.3V
   - 按本机 PTT → 全程约 3.3V
   - 按监听键强开静噪 → 全程约 3.3V（验证 `FUNCTION_MONITOR` 排除逻辑）
5. 该固件插 USB-C，确认 UV Studio 刷机、校准备份、K5Viewer、CHIRP 仍正常。

本项目无自动化测试框架，上述 1–3 为可自动执行的门禁，4–5 为人工台架验证。

## 已知限制（写入 README）

- **K 头串口失效**：用肯伍德双插头编程线做 CHIRP 读写、UV Studio 校准备份、
  K5Viewer 全部不可用，必须改走 USB-C。DFU 刷机不受影响 —— Bootloader 自己重新
  配置 PA9/PA10，与 App 无关。
- **省电模式拖慢响应**：BatSav 开启时手台按占空比休眠，Q_OUT 最坏会晚几百毫秒
  拉低。做热点用建议关闭 BatSav。
- **开启 CTCSS/DCS 时 Q_OUT 滞后**：`INCOMING→RECEIVE` 需等亚音解码确认，
  几十到几百毫秒，外部设备会漏掉信号开头。信道不设亚音时二者实际同时发生。
- **Q_OUT 比音频通路晚约 1 ms**（见决策 3）。已评估接受。
- **不区分 VFO**：双守候或扫描状态下，任一 VFO 的有效接收都会拉低 Q_OUT。

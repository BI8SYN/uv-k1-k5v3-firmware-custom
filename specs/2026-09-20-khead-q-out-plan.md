# K 头 Q_OUT 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 PA9（USART1_TX，K 头 2.5mm Ring）改造成低电平有效、开漏输出的有效接收指示信号 Q_OUT，使 K 头可作为标准模拟热点接口。

**Architecture:** 三处代码改动，全部包在默认关闭的 `ENABLE_KHEAD_Q_OUT` 编译开关内：`App/driver/gpio.h` 提供引脚定义和 `GPIO_SetQOut()` 内联包装；`App/board.c` 的 `BOARD_GPIO_Init()` 把 PA9 配成开漏+内部上拉输出（并把失去初始化者的 PA10 泊成上拉输入）；`App/functions.c` 的 `FUNCTION_Select()` 在唯一的 `gCurrentFunction` 赋值点后跟随 `FUNCTION_RECEIVE` 驱动该引脚。第四处是 `App/CMakeLists.txt` 的硬性构建门禁，阻止与 `ENABLE_UART` 同时开启。

**Tech Stack:** C11，PY32F071 LL 驱动，CMake + Ninja，Docker 内的 arm-gnu-toolchain 13.3.rel1。

**Spec:** `specs/2026-09-20-khead-q-out-design.md`

## Global Constraints

- 所有新增代码必须包在 `#ifdef ENABLE_KHEAD_Q_OUT` 内。默认关闭时改动必须完全惰性。
- 开关命名为 `ENABLE_KHEAD_Q_OUT`，**不带** `FEAT_F4HWN_` 前缀（这不是 F4HWN 上游功能，不冒用其命名空间）。
- Q_OUT 低电平有效：`FUNCTION_RECEIVE` → 拉低；其余全部状态 → 释放到高。
- Q_OUT 只跟随 `gCurrentFunction == FUNCTION_RECEIVE`，**不得**使用 `FUNCTION_IsRx()`（它包含 `FUNCTION_MONITOR`）。
- PA9 电气配置固定为：`LL_GPIO_MODE_OUTPUT` + `LL_GPIO_OUTPUT_OPENDRAIN` + `LL_GPIO_PULL_UP`。
- `ENABLE_KHEAD_Q_OUT` 与 `ENABLE_UART` 互斥，且必须在**构建期**失败，不能留到运行期。
- 构建产物必须满足 `compile-firmware.sh` 自带的上限：Flash ≤ 118 KiB（120832 B），RAM ≤ 16 KiB（16384 B）。
- 注释用英文，与周围代码一致。缩进 4 空格，与 `App/` 下现有文件一致。

## 本项目的"测试"是什么

本固件仓库没有单元测试框架，无法对 MCU 代码做主机侧断言。因此每个任务的红/绿循环由**构建门禁**和**目标码静态验证**承担：

- 构建成功/失败（含期望的 `FATAL_ERROR`）
- `arm-none-eabi-objdump` 反汇编确认代码确实被编译进目标函数
- `compile-firmware.sh` 自带的 Flash/RAM 上限检查

台架实测（万用表）由人工在任务 5 完成，不属于自动化门禁。

所有构建命令均通过 Docker 执行。镜像 `uvk1-uvk5v3` 由 `compile-firmware.sh` 首次运行时自动构建。

## 文件结构

| 文件 | 职责 | 动作 |
|---|---|---|
| `App/driver/gpio.h` | 引脚编码 + 单一策略无关的电平包装 `GPIO_SetQOut(bool)` | 修改 |
| `App/board.c` | 上电时的 PA9/PA10 引脚模式配置 | 修改 |
| `App/functions.c` | 唯一的策略点：什么状态算"有效接收" | 修改 |
| `App/CMakeLists.txt` | 开关注册 + 与 UART 的互斥门禁 | 修改 |
| `CMakePresets.json` | 开关默认值 `false` | 修改 |
| `README.md` | 选项说明、接线图、已知限制 | 修改 |

不新建源文件。`GPIO_SetQOut()` 作为内联函数放进 `gpio.h`，与现有的 `GPIO_EnableAudioPath()` / `GPIO_TurnOnBacklight()` / `GPIO_IsPttPressed()` 并列，遵循该文件既有模式；为三行代码新建 `.c` 文件不符合本仓库风格。

---

### Task 1: 构建门禁（互斥保护）

先做这一条，因为它是后续所有构建命令的安全网：一旦它生效，任何"UART 和 Q_OUT 同时开着"的误操作都会在 configure 阶段就炸掉，而不是编译出一个 Q_OUT 静默失效的固件。

**Files:**
- Modify: `App/CMakeLists.txt`（在 `enable_feature(ENABLE_UART ...)` 之后，约第 97-101 行附近）

**Interfaces:**
- Consumes: 无
- Produces: CMake 缓存变量 `ENABLE_KHEAD_Q_OUT`；开启时向 `App` 接口库注入同名编译宏 `ENABLE_KHEAD_Q_OUT`

- [ ] **Step 1: 先跑一次"失败测试"——确认门禁尚不存在**

```bash
cd /Users/liyongsheng/projects/uv-k1-k5v3-firmware-custom
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/src -w /src uvk1-uvk5v3 \
  cmake --fresh --preset Fusion -DENABLE_KHEAD_Q_OUT=ON 2>&1 | tail -5
```

Expected: configure **成功**（这就是"红"——错误的组合目前被放行）。

- [ ] **Step 2: 加入开关注册与门禁**

在 `App/CMakeLists.txt` 中，紧接现有的

```cmake
enable_feature(ENABLE_UART
    driver/uart.c
    # driver/aes.c
)
```

之后插入：

```cmake
# ENABLE_KHEAD_Q_OUT repurposes PA9 as an active-low valid-receive indicator on
# the K-head 2.5mm ring. USART1_TX uses the same pin, and UART_Init() runs after
# BOARD_Init(), so with both enabled the UART would silently reclaim PA9 as AF1:
# the firmware builds, boots, and Q_OUT simply never moves. Fail at configure
# time instead.
if(ENABLE_KHEAD_Q_OUT AND ENABLE_UART)
    message(FATAL_ERROR
        "ENABLE_KHEAD_Q_OUT needs PA9, which USART1_TX also uses. "
        "Build with -DENABLE_UART=OFF.")
endif()

enable_feature(ENABLE_KHEAD_Q_OUT)
```

- [ ] **Step 3: 跑"绿"——门禁必须拦截错误组合**

```bash
cd /Users/liyongsheng/projects/uv-k1-k5v3-firmware-custom
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/src -w /src uvk1-uvk5v3 \
  cmake --fresh --preset Fusion -DENABLE_KHEAD_Q_OUT=ON 2>&1 | tail -8
```

Expected: 以 `CMake Error` / `FATAL_ERROR` 失败，信息中含 `ENABLE_KHEAD_Q_OUT needs PA9`。

- [ ] **Step 4: 确认正确组合仍可 configure**

```bash
cd /Users/liyongsheng/projects/uv-k1-k5v3-firmware-custom
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/src -w /src uvk1-uvk5v3 \
  cmake --fresh --preset Fusion -DENABLE_UART=OFF -DENABLE_KHEAD_Q_OUT=ON 2>&1 | tail -5
```

Expected: `-- Generating done` / `-- Build files have been written to`，无错误。

- [ ] **Step 5: 确认默认（两者都不开 Q_OUT）仍可 configure**

```bash
cd /Users/liyongsheng/projects/uv-k1-k5v3-firmware-custom
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/src -w /src uvk1-uvk5v3 \
  cmake --fresh --preset Fusion 2>&1 | tail -5
```

Expected: 成功。

- [ ] **Step 6: 提交**

```bash
cd /Users/liyongsheng/projects/uv-k1-k5v3-firmware-custom
git add App/CMakeLists.txt
git commit -m "Add ENABLE_KHEAD_Q_OUT switch with hard UART mutual-exclusion gate

PA9 is USART1_TX. UART_Init() runs after BOARD_Init(), so enabling both
would let the UART silently reclaim the pin and leave Q_OUT dead in a
firmware that otherwise builds and boots. Reject the combination at
configure time.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: 引脚定义与电平包装

**Files:**
- Modify: `App/driver/gpio.h:29-34`（`GPIO_PINS` 枚举）与文件末尾的内联包装区

**Interfaces:**
- Consumes: Task 1 的 `ENABLE_KHEAD_Q_OUT` 宏
- Produces:
  - `GPIO_PIN_Q_OUT`（`enum GPIO_PINS` 成员，值为 `GPIO_MAKE_PIN(GPIOA, LL_GPIO_PIN_9)`）
  - `static inline void GPIO_SetQOut(bool active)` —— `active == true` 拉低，`false` 释放为高。Task 4 是它唯一的调用者。

- [ ] **Step 1: 在 `GPIO_PINS` 枚举中加入引脚**

把 `App/driver/gpio.h` 的枚举改成：

```c
enum GPIO_PINS
{
    GPIO_PIN_PTT            = GPIO_MAKE_PIN(GPIOB, LL_GPIO_PIN_10),
    GPIO_PIN_BACKLIGHT      = GPIO_MAKE_PIN(GPIOF, LL_GPIO_PIN_8),
    GPIO_PIN_FLASHLIGHT     = GPIO_MAKE_PIN(GPIOC, LL_GPIO_PIN_13),
    GPIO_PIN_AUDIO_PATH     = GPIO_MAKE_PIN(GPIOA, LL_GPIO_PIN_8),
#ifdef ENABLE_KHEAD_Q_OUT
    GPIO_PIN_Q_OUT          = GPIO_MAKE_PIN(GPIOA, LL_GPIO_PIN_9),
#endif
};
```

- [ ] **Step 2: 加入电平包装**

在 `App/driver/gpio.h` 中 `GPIO_IsPttPressed()` 之后、`#endif` 之前插入：

```c
#ifdef ENABLE_KHEAD_Q_OUT

// Valid-receive indicator on the K-head 2.5mm ring (PA9).
//
// Active low and open drain: asserting pulls the line to GND, releasing hands
// it back to the pull-up so an external device can bias it to 3.3 V or 5 V, or
// drive a transistor, MOSFET or analog switch directly.
static inline void GPIO_SetQOut(bool active)
{
    if (active)
        GPIO_ResetOutputPin(GPIO_PIN_Q_OUT);
    else
        GPIO_SetOutputPin(GPIO_PIN_Q_OUT);
}

#endif // ENABLE_KHEAD_Q_OUT
```

`gpio.h` 顶部已 `#include <stdbool.h>`，`bool` 可直接使用。

- [ ] **Step 3: 确认默认构建不受影响**

```bash
cd /Users/liyongsheng/projects/uv-k1-k5v3-firmware-custom
./compile-firmware.sh Fusion 2>&1 | tail -15
```

Expected: 构建成功，Flash/RAM 在上限内。此时 `GPIO_SetQOut` 尚无调用者，但因为在 `#ifdef` 内且默认关闭，不会产生 unused-function 警告。

- [ ] **Step 4: 提交**

```bash
cd /Users/liyongsheng/projects/uv-k1-k5v3-firmware-custom
git add App/driver/gpio.h
git commit -m "Add GPIO_PIN_Q_OUT and GPIO_SetQOut() behind ENABLE_KHEAD_Q_OUT

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: 上电引脚配置

**Files:**
- Modify: `App/board.c`，在 `BOARD_GPIO_Init()` 末尾（现有 `#ifndef ENABLE_SWD` 块之后、函数收尾 `}` 之前）

**Interfaces:**
- Consumes: Task 1 的 `ENABLE_KHEAD_Q_OUT` 宏。**不**使用 Task 2 的 `GPIO_PIN_Q_OUT`——`LL_GPIO_Init()` 需要端口和引脚掩码分开传，`board.c` 全文都是直接写 `GPIOA` + `LL_GPIO_PIN_n`，保持一致。
- Produces: 上电后 PA9 处于开漏+上拉输出、未有效（高）状态；PA10 处于上拉输入

- [ ] **Step 1: 加入配置块**

在 `App/board.c` 的 `BOARD_GPIO_Init()` 内，`#ifndef ENABLE_SWD ... #endif // ENABLE_SWD` 之后插入：

```c
#ifdef ENABLE_KHEAD_Q_OUT
    // K-head 2.5mm ring (PA9) becomes the valid-receive indicator instead of
    // USART1_TX. Drive the pin inactive before switching it to an output so
    // power-up does not emit a spurious low pulse.
    //
    // PA10 was USART1_RX. With ENABLE_UART off nothing initialises it any more
    // and it would sit floating on an externally exposed connector, so park it
    // at the UART idle level.
    do
    {
        LL_GPIO_InitTypeDef QOutInit;
        LL_GPIO_StructInit(&QOutInit);

        LL_GPIO_SetOutputPin(GPIOA, LL_GPIO_PIN_9);

        QOutInit.Pin        = LL_GPIO_PIN_9;
        QOutInit.Mode       = LL_GPIO_MODE_OUTPUT;
        QOutInit.OutputType = LL_GPIO_OUTPUT_OPENDRAIN;
        QOutInit.Pull       = LL_GPIO_PULL_UP;
        QOutInit.Speed      = LL_GPIO_SPEED_FREQ_LOW;
        LL_GPIO_Init(GPIOA, &QOutInit);

        LL_GPIO_StructInit(&QOutInit);
        QOutInit.Pin  = LL_GPIO_PIN_10;
        QOutInit.Mode = LL_GPIO_MODE_INPUT;
        QOutInit.Pull = LL_GPIO_PULL_UP;
        LL_GPIO_Init(GPIOA, &QOutInit);

    } while (0);
#endif // ENABLE_KHEAD_Q_OUT
```

用自己的 `LL_GPIO_InitTypeDef` 而非复用函数中那个 `InitStruct`，是为了不把 `OutputType = OPENDRAIN` 泄漏给后续可能新增的代码。`LL_GPIO_StructInit()` 在两次使用之间重置，避免 `OPENDRAIN` 带到 PA10 的配置上。

**注意**：`BOARD_GPIO_Init()` 必须已经使能了 GPIOA 的时钟。确认函数开头有 `LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA)`；若没有，在上述块开头补上。

- [ ] **Step 2: 确认 GPIOA 时钟已使能**

```bash
cd /Users/liyongsheng/projects/uv-k1-k5v3-firmware-custom
sed -n '/void BOARD_GPIO_Init/,/^}/p' App/board.c | grep -n "EnableClock"
```

Expected: 输出中包含 `LL_IOP_GRP1_PERIPH_GPIOA`。若不包含，在 Step 1 的 `do` 块首行加入
`LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA);`。

- [ ] **Step 3: 构建 Q_OUT 固件**

```bash
cd /Users/liyongsheng/projects/uv-k1-k5v3-firmware-custom
./compile-firmware.sh Fusion -DENABLE_UART=OFF -DENABLE_KHEAD_Q_OUT=ON 2>&1 | tail -15
```

Expected: 构建成功，Flash/RAM 在上限内。

- [ ] **Step 4: 静态验证——`BOARD_GPIO_Init` 体积必须增大**

`ENABLE_UART` 不参与 `BOARD_GPIO_Init` 的编译，所以两次构建之间该函数体积的差异只能来自本任务。先量开关关闭时的基线：

```bash
cd /Users/liyongsheng/projects/uv-k1-k5v3-firmware-custom
./compile-firmware.sh Fusion >/dev/null 2>&1
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/src -w /src uvk1-uvk5v3 \
  arm-none-eabi-nm -S build/Fusion/f4hwn.fusion.elf | grep -i " BOARD_GPIO_Init$"
```

记下第二列（十六进制体积）。再量开启时：

```bash
cd /Users/liyongsheng/projects/uv-k1-k5v3-firmware-custom
./compile-firmware.sh Fusion -DENABLE_UART=OFF -DENABLE_KHEAD_Q_OUT=ON >/dev/null 2>&1
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/src -w /src uvk1-uvk5v3 \
  arm-none-eabi-nm -S build/Fusion/f4hwn.fusion.elf | grep -i " BOARD_GPIO_Init$"
```

Expected: 开启时的体积**严格大于**基线。把两个数字都记进任务记录。若符号不存在（被内联），改用
`arm-none-eabi-nm -S ... | grep -i board_gpio` 确认实际符号名后重试。

- [ ] **Step 5: 提交**

```bash
cd /Users/liyongsheng/projects/uv-k1-k5v3-firmware-custom
git add App/board.c
git commit -m "Configure PA9 as open-drain Q_OUT and park PA10 at boot

PA9 is driven inactive before it becomes an output so power-up emits no
spurious low pulse. PA10 loses its USART1_RX initialiser when ENABLE_UART
is off, so park it pulled up rather than leave an externally exposed pin
floating.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: 状态钩子

这是整个功能唯一的策略点。

**Files:**
- Modify: `App/functions.c:229`（`FUNCTION_Select()` 中 `gCurrentFunction = Function;` 之后）

**Interfaces:**
- Consumes: Task 2 的 `GPIO_SetQOut(bool)`
- Produces: 运行期行为——`FUNCTION_RECEIVE` 时 PA9 拉低，其余状态释放为高

- [ ] **Step 1: 插入钩子**

在 `App/functions.c` 中找到：

```c
    gCurrentFunction = Function;
```

改为：

```c
    gCurrentFunction = Function;

#ifdef ENABLE_KHEAD_Q_OUT
    // Track real RF reception only. FUNCTION_INCOMING means squelch opened but
    // CTCSS/DCS has not been validated yet, and FUNCTION_MONITOR means the user
    // forced squelch open by hand - neither is a valid-receive indication, so
    // FUNCTION_IsRx() is deliberately not used here.
    //
    // This sits immediately after the assignment because the rest of
    // FUNCTION_Select() returns early for FOREGROUND and POWER_SAVE; only here
    // is every state transition covered.
    GPIO_SetQOut(Function == FUNCTION_RECEIVE);
#endif
```

`App/functions.c` 已 `#include "driver/gpio.h"`，无需新增包含。

- [ ] **Step 2: 构建**

```bash
cd /Users/liyongsheng/projects/uv-k1-k5v3-firmware-custom
./compile-firmware.sh Fusion -DENABLE_UART=OFF -DENABLE_KHEAD_Q_OUT=ON 2>&1 | tail -15
```

Expected: 构建成功，Flash/RAM 在上限内。

- [ ] **Step 3: 静态验证钩子确实进了 `FUNCTION_Select`，且默认关闭时不在**

`ENABLE_UART` 不参与 `FUNCTION_Select` 的编译，所以该函数体积的差异只能来自本任务。

```bash
cd /Users/liyongsheng/projects/uv-k1-k5v3-firmware-custom
echo "--- Q_OUT OFF (baseline) ---"
./compile-firmware.sh Fusion >/dev/null 2>&1
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/src -w /src uvk1-uvk5v3 \
  arm-none-eabi-nm -S build/Fusion/f4hwn.fusion.elf | grep -i " FUNCTION_Select$"
echo "--- Q_OUT ON ---"
./compile-firmware.sh Fusion -DENABLE_UART=OFF -DENABLE_KHEAD_Q_OUT=ON >/dev/null 2>&1
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/src -w /src uvk1-uvk5v3 \
  arm-none-eabi-nm -S build/Fusion/f4hwn.fusion.elf | grep -i " FUNCTION_Select$"
```

Expected: 开启时体积严格大于基线。这一条同时证明了两件事——钩子编译进去了，且默认关闭时它确实不存在（惰性）。把两个数字记进任务记录。

- [ ] **Step 4: 反汇编确认写的是 GPIOA 而非别的端口**

```bash
cd /Users/liyongsheng/projects/uv-k1-k5v3-firmware-custom
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/src -w /src uvk1-uvk5v3 \
  arm-none-eabi-objdump -d --disassemble='FUNCTION_Select' build/Fusion/f4hwn.fusion.elf
```

Expected: 函数体内出现 GPIOA 基址 `0x48000000` 的字面量，以及 `0x200`（`LL_GPIO_PIN_9` 掩码）。注意此时 build 目录是 Q_OUT ON 的那次构建（Step 3 的第二次），顺序不要颠倒。把相关几行贴进任务记录。

- [ ] **Step 5: 提交**

```bash
cd /Users/liyongsheng/projects/uv-k1-k5v3-firmware-custom
git add App/functions.c
git commit -m "Drive Q_OUT from FUNCTION_RECEIVE in FUNCTION_Select()

gCurrentFunction is assigned in exactly one place, so hooking the line
right after it covers every transition including TX, power save and scan.
FUNCTION_IsRx() is deliberately avoided: it includes FUNCTION_MONITOR,
which is a hand-forced squelch, not real RF reception.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 5: 开关默认值、文档与全量回归

**Files:**
- Modify: `CMakePresets.json`（default preset 的 `cacheVariables`）
- Modify: `README.md`

**Interfaces:**
- Consumes: Task 1-4 的全部成果
- Produces: 最终可交付固件

- [ ] **Step 1: 在 default preset 中登记开关**

在 `CMakePresets.json` 的 `default` preset `cacheVariables` 中，紧挨 `"ENABLE_UART": true` 之后加入：

```json
                "ENABLE_KHEAD_Q_OUT": false,
```

- [ ] **Step 2: 验证 JSON 合法**

```bash
cd /Users/liyongsheng/projects/uv-k1-k5v3-firmware-custom
python3 -c "import json; d=json.load(open('CMakePresets.json')); print(d['configurePresets'][0]['cacheVariables']['ENABLE_KHEAD_Q_OUT'])"
```

Expected: 输出 `False`。

- [ ] **Step 3: 补 README**

在 `README.md` 的编译选项章节加入一节（位置：紧随现有 compile options 表格或章节之后）：

````markdown
### K-head Q_OUT (valid-receive indicator)

`ENABLE_KHEAD_Q_OUT` repurposes PA9 - normally USART1_TX, wired to the K-head
2.5mm ring - as an active-low, open-drain indication that the radio is actually
receiving a valid signal. This turns the K-head into a standard analog hotspot
interface:

```
2.5mm Tip     = SPK OUT
2.5mm Ring    = Q OUT      <- this option
2.5mm Sleeve  = GND
3.5mm Ring    = MIC IN
3.5mm Sleeve  = PTT IN
```

The pin is released (pulled up, ~3.3 V) at all times except while
`FUNCTION_RECEIVE` is active, when it is pulled to GND. Manual monitor
(`FUNCTION_MONITOR`) deliberately does **not** assert it. Being open drain, an
external device is free to bias the line to 3.3 V or 5 V, or to drive a
transistor, MOSFET or analog switch directly.

Build it with:

```bash
./compile-firmware.sh Fusion -DENABLE_UART=OFF -DENABLE_KHEAD_Q_OUT=ON
```

`ENABLE_UART=OFF` is mandatory - the build fails otherwise, because USART1_TX
claims the same pin.

**Known limitations**

- **The K-head serial port is gone.** CHIRP, UV Studio calibration backup and
  K5Viewer must go over USB-C instead of a Kenwood two-plug programming cable.
  DFU flashing is unaffected: the bootloader reconfigures PA9/PA10 itself.
- **Battery save delays the indication.** With BatSav on, the radio sleeps on a
  duty cycle and Q_OUT can assert several hundred milliseconds late. Turn
  BatSav off for hotspot use.
- **CTCSS/DCS delays the indication.** `FUNCTION_INCOMING` waits for the
  subaudible decoder before promoting to `FUNCTION_RECEIVE`, so Q_OUT lags the
  audio by tens to hundreds of milliseconds and the external device misses the
  head of the transmission. On a channel with no subaudible tone the two happen
  in the same main-loop iteration.
- **Q_OUT trails the audio path by roughly 1 ms**, the time `APP_StartListening()`
  spends bit-banging BK4819 registers before it calls `FUNCTION_Select()`.
- **No per-VFO distinction.** Under dual watch or scan, valid reception on
  either VFO asserts Q_OUT.
````

- [ ] **Step 4: 全量回归——5 个发布目标必须全绿**

```bash
cd /Users/liyongsheng/projects/uv-k1-k5v3-firmware-custom
./compile-firmware.sh All 2>&1 | tail -25
```

Expected: Fusion、Transfer、FieldOps、Labs、Max 全部成功，且各自的 Flash/RAM 在上限内。这是"默认关闭时改动惰性"的端到端证据。

- [ ] **Step 5: 反向门禁回归**

```bash
cd /Users/liyongsheng/projects/uv-k1-k5v3-firmware-custom
./compile-firmware.sh Fusion -DENABLE_KHEAD_Q_OUT=ON 2>&1 | tail -8
```

Expected: 失败，信息中含 `ENABLE_KHEAD_Q_OUT needs PA9`。

- [ ] **Step 6: 产出交付固件**

```bash
cd /Users/liyongsheng/projects/uv-k1-k5v3-firmware-custom
./compile-firmware.sh Fusion -DENABLE_UART=OFF -DENABLE_KHEAD_Q_OUT=ON 2>&1 | tail -15
ls -l build/Fusion/f4hwn.fusion.bin build/Fusion/f4hwn.fusion.hex
```

Expected: 两个文件都存在。把 `.bin` 复制到一个显眼的交付路径并报告其大小与 SHA256。

- [ ] **Step 7: 提交**

```bash
cd /Users/liyongsheng/projects/uv-k1-k5v3-firmware-custom
git add CMakePresets.json README.md
git commit -m "Register ENABLE_KHEAD_Q_OUT default-off and document it

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

## 人工台架验证（交付后由用户执行，非自动化门禁）

万用表红表笔接 2.5mm Ring，黑表笔接 2.5mm Sleeve：

| 操作 | 期望读数 |
|---|---|
| 开机待机 | 约 3.3 V |
| 另一台手台向本机发射 | 0 V |
| 对方松开 PTT | 回到约 3.3 V |
| 按本机 PTT 发射 | 全程约 3.3 V |
| 按监听键强开静噪 | 全程约 3.3 V |

再插 USB-C，确认 UV Studio 刷机、校准备份、K5Viewer、CHIRP 仍可用。

若"按监听键"那一行读到 0 V，说明钩子误用了 `FUNCTION_IsRx()` 而非
`Function == FUNCTION_RECEIVE`，回到 Task 4 检查。

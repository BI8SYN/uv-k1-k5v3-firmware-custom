# AI 协作说明

## Ubuntu 构建（默认入口）

- 默认在 OrbStack Ubuntu `embedded` 中构建，Linux 用户 `johnson`；源码保留在 Mac 可直接访问的目录。
- 从仓库根目录运行 `tools/build-ubuntu.sh`，不要默认调用 macOS 工具链、Docker 或旧的本机构建入口。
- 默认配置：`Fusion`。可选配置：`Custom / Fusion / FieldOps / Transfer / Labs / Fusion_ / Service_ / Max`；以 `tools/build-ubuntu.sh --help` 为准。
- Mac 调用会自动进入 Ubuntu；在 Ubuntu 内执行同一脚本也可以。机器或用户名变化时，可设置 `EMBEDDED_MACHINE` / `EMBEDDED_USER`；并行数用 `BUILD_JOBS`，默认 4。
- 工具链与依赖在 Linux 内，构建缓存位于 `~/build/uv-k1-k5v3-firmware-custom/<checkout-hash>/<variant>/`。不同工作副本使用独立缓存。
- 成功产物导出到本项目 `.ubuntu-build/<variant>/`，查看 `build-info.json` 和 `SHA256SUMS`；命令失败时不得把旧固件当成新结果。
- 此入口只执行构建／本机测试，不执行烧录、串口监视、签名、发布、Git 提交或推送。硬件验证按用户当前任务和本文件已有授权另行执行。
- 不复制 macOS 的可执行工具或 Python venv 到 Linux；工具链升级需遵守项目现有版本要求。

## embedded 设备操作（默认）

- 编译、烧录、Flash 备份与串口监视统一在 OrbStack `embedded` / `johnson` 内运行。Mac 只运行系统 Python + OrbStack 分发，不使用 Mac ESP-IDF、PlatformIO 或其 Python 环境，也不得默默回退到本机工具。
- 统一入口 `tools/device-ubuntu.sh --help`。`ports` 列出 Linux 串口，`version` 检查 Linux 工具。`ESP_PORT` 指定 Linux 设备路径，只有恰好一个设备时才自动选择；多个设备必须明确指定。
- USB 串口可能自动转发为 `/dev/cu.usbmodem…`，USB 独占直通后一般是 `/dev/ttyACM0` / `/dev/ttyUSB0`。用 `orb serial list` / `orb usb list` 核对，禁止把构建或 `--help` 当作设备验证。 需要独占直通时，先核对目标设备的 VID/PID/序列号，再执行 `orb usb attach <USB-ID> -m embedded`，随后重新运行 `ports`；不得选择外挂存储设备。
- `EMBEDDED_VARIANT` 选择与 `build-ubuntu.sh` 相同的板型；radio 也支持 `RADIO_BOARD`。跨板型不可复用烧录地址。默认 esptool 波特率 115200；实测稳定的原生 USB 可显式 `ESP_BAUD=921600`，串口监视默认 115200，Ctrl-] 退出。
- Mac 的绝对文件路径会映射到 Linux `/mnt/mac/…`；从项目根目录调用入口，源码和输出仍然由 Mac/Codex 直接访问。

- UV-K1 使用自身协议，不能调用 ESP32 的 esptool。入口为 `tools/serial-ubuntu.sh --help`，运行 Linux pyserial 和仓库原有 serialtool；端口由 `ESP_PORT` 选择。原有 bootloader、校准、Overlay 槽位的保护边界保持不变。
- 本轮没有连接 UV-K1，只有协议工具帮助/导入检查通过，不能声称已完成 UV-K1 实机烧录验证。

- GitHub Actions 的 IDF Linux 容器继续使用已激活的 IDF_PATH/Python 和仓库 build/；这是已有 CI 发布流程，Mac 日常开发仍默认 embedded。

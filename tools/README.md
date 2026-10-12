# 开发工具

从仓库根目录运行工具，输入和输出路径相对于当前工作目录。

- `flash.py`：macOS / Linux 串口 Bootloader 刷机，只写原始固件，不操作 EEPROM。
- `fw-pack.py`：固件打包，由 Makefile 自动调用。
- `gains.py`：生成 BK4819 增益表。
- `map_analyze.py`：分析链接器 map 文件中的空间占用。
- `hawk5.py`：历史 Hawk5 CHIRP 驱动模块。
- `ui-preview.py` / `ui-preview.c`：使用固件绘图代码生成 VFO 布局预览。

```sh
python3 tools/gains.py
python3 tools/map_analyze.py obj/output.map 20
```

## UI 预览

```sh
python3 tools/ui-preview.py
python3 tools/ui-preview.py --output /tmp/vfo-preview.png --scale 4
python3 tools/ui-preview.py --frames frequency name --output /tmp/vfo-name.png
python3 tools/ui-preview.py --frames max maxtext --output /tmp/vfo-max.png
```

默认输出 `docs/images/vfo-states.png`，左侧为频率状态，右侧为名称状态，均展示两个组件的布局。`--scale` 支持 1～12 倍，默认 4 倍；`--frames` 可选择左右预览场景：`single`（名称状态）、`plain`（无名称）、`dual`、`max`（最长频率）、`maxtext`（最长频率带名称）、`lw`、`frequency`（双 VFO 频率状态）、`name`（双 VFO 名称状态）、`tuned`（重新调谐）、`tx`（发射布局）、`seek`（搜索）、`rds`（模拟数据组经过实际解析器后的输出）。示例频率、名称、RDS 和信号值在 `tools/ui-preview.c` 中修改。

需要 Python 3 和主机 C 编译器（默认 `cc`，可通过 `CC` 指定），无额外 Python 依赖，也无需 ARM 工具链或连接对讲机。脚本在临时目录编译当前固件的绘图代码，运行后自动清理；预览使用示例数据。

## 刷机

使用标准 UV-K5/K6、DP32G030 Bootloader，Python 3 无额外串口依赖。连接编程线，关闭对讲机电源，按住 PTT 开机进入 Bootloader，然后运行：

```sh
make flash PORT=/dev/cu.usbserial-1420
# Linux 示例
make flash PORT=/dev/ttyUSB0
```

`PORT` 必须显式指定；macOS 可用 `ls /dev/cu.*` 查找设备。命令先编译，随后发送 `bin/firmware.bin`，不能替换为 `.packed.bin`。脚本在打开串口后最多等待 45 秒握手，等待期间每秒输出一个 `.`，收到握手立即开始刷写，以 38400 baud 传输；每块等待应答，超时最多尝试 5 次。全部应答后提示完成，再关闭并重新开启对讲机。

失败或中断时，重新进入 Bootloader，再运行完整命令。完成提示表示各块得到 Bootloader 确认，没有执行 Flash 回读校验。该命令不初始化 EEPROM。

独立调用：`python3 tools/flash.py --port /dev/ttyUSB0 --firmware bin/firmware.bin`。
主机模拟测试：`python3 tests/flash-regression.py`，不访问设备。

A/B 首屏切换预览：`python3 tools/ui-preview.py --frames ab-a ab-b --output docs/images/ab-vfo.png`，左图选中 A，右图选中 B，使用固件真实组件及字体绘制。

多 VFO 分页：`python3 tools/ui-preview.py --frames multi-ab multi-cd --output docs/images/multi-vfo.png`。双守听探测／接收归属：`python3 tools/ui-preview.py --frames watch-probe watch-rx --output docs/images/dual-watch.png`。

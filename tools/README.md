# 开发工具

从仓库根目录运行工具，输入和输出路径相对于当前工作目录。

- `flash.py`：macOS / Linux 串口 Bootloader 刷机，只写原始固件，不操作 EEPROM。
- `fw-pack.py`：固件打包，由 Makefile 自动调用。
- `gains.py`：生成 BK4819 增益表。
- `map_analyze.py`：分析链接器 map 文件中的空间占用。
- `hawk5.py`：历史 Hawk5 CHIRP 驱动模块。

```sh
python3 tools/gains.py
python3 tools/map_analyze.py obj/output.map 20
```

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

# UV-K1 首启与只读备份

首版只检查屏幕、键盘、背光、电池与 UART/SPI 读取。TX 强制关闭，外部 Flash 没有任何写入/擦除路径。USB CDC 尚未实现，刷机和导出都走 K 头 UART 线（38400 baud）。

## 构建与刷机

```sh
make BOARD=uvk1
make BOARD=uvk6
python3 tools/flash.py --board uvk1 --port /dev/cu.usbserial-1420 --firmware bin/uvk1/firmware.bin
```

按住 PTT 开机进入 Bootloader。工具只接受 7.x；全部页面 ACK 后关机，再松开 PTT 正常开机。K1 不使用 `firmware.packed.bin`。

## 首启验收

- 屏幕内容正常、无乱码，背光亮起，按键可唤醒背光。
- 顶部 `SPI EB6015 RO`；诊断页显示 `Storage read-only`。
- 依次检查数字键、MENU/UP/DOWN/EXIT/*/F、两侧键和 PTT；`KEY 编号/状态` 应变化。编号 0–9 为数字，10–15 为 MENU/UP/DOWN/EXIT/*/F，21 为 PTT，22/23 为侧键；状态 1 按下、2 长按、3 连续长按、0 松开。PTT 不发射。
- 电池读数合理，并与设备实际电压核对。已刷入的首版使用默认换算值 2000；最新离线构建会在首启诊断时读取原厂校准区六个 ADC 点，以第 4 点（偏移 `0x146`）作为 7.60 V 基准。无效校准保留默认值，已有有效用户配置不被覆盖。此更新尚未刷入，充电电流暂为 0。
- 如显示 `Storage unknown`，先用 `info` 获取实际 ID，不进行存储初始化或写入。

## 两次物理备份

上述验收通过后，保持正常开机并接 K 口 UART。先确认 ID 和容量，然后读取两次；下面文件名为首次验收示例，已有文件时请换名，避免覆盖。

```sh
python3 tools/resources.py --port /dev/cu.usbserial-1420 info
mkdir -p backups/uvk1
python3 tools/resources.py --port /dev/cu.usbserial-1420 read 0 0x200000 backups/uvk1/uvk1-spi-2MiB-read1.bin
python3 tools/resources.py --port /dev/cu.usbserial-1420 read 0 0x200000 backups/uvk1/uvk1-spi-2MiB-read2.bin
cmp backups/uvk1/uvk1-spi-2MiB-read1.bin backups/uvk1/uvk1-spi-2MiB-read2.bin
shasum -a 256 backups/uvk1/uvk1-spi-2MiB-read1.bin backups/uvk1/uvk1-spi-2MiB-read2.bin
wc -c backups/uvk1/uvk1-spi-2MiB-read1.bin backups/uvk1/uvk1-spi-2MiB-read2.bin
```

`info` 应报告 2097152 B；两份文件各 2097152 B，`cmp` 成功且 SHA-256 相同才算完成。UART 38400 读整片需要较长时间。若读取中断，重新连接后对同一个 `read` 命令追加 `--resume`；不要修改地址、长度或输出名。检查点每 4 KiB 写入主机磁盘，不会写设备。工具拒绝覆盖已存在的完整输出。默认每块读取前间隔 20 ms，超时 1 s；每块最多重试 2 次，整个连接总共最多重试 6 次，并在重试前退避 250 ms。达到任一限制即停止；`read --no-retry` 可以禁用自动重试。连接恢复后须显式执行 `--resume`，工具不会自动重连或重置设备。保存 SHA-256 与验收结果，并异地保存两份备份；`backups/` 被 Git 忽略。

备份的物理 `0x010000–0x0101FF` 应与此前逻辑 `0x1E00–0x1FFF` 的 512 B 校准备份比较。这里是 SPI Flash 的物理地址，不能把旧的逻辑 64 KiB 备份视为整片备份。两次读取不一致或容量/ID 不符时，先排查读取链路，不开放写入。

## 验收记录

- 双板构建、主机回归、472 页格式模拟测试、只读存储越界/拒写测试：已通过。
- 首次刷入：2026-10-19，经 K 口 UART、Bootloader 7.02.02，53,220 B 全部页面 ACK。
- 首次开机屏幕已显示，UART 可通信；JEDEC ID 本机实测为 `EB 60 15`。首版因 ID 白名单过窄显示 `Storage unknown`，修正版加入该 ID。
- 修正版刷入：2026-10-19，53,232 B 全部页面 ACK，加入本机 `EB6015`。
- 修正版正常开机：用户确认显示正常、按键正常，电池显示 8.64 V（默认校准换算，精度待核对）。UART `info` 实测返回 `SPI EB6015 RO: 2097152 bytes`。
- 两次整片备份：未完成。第一遍在约 62%（`0x145200`）连续无应答后中断，macOS 随后拒绝串口参数设置（`Invalid argument`），需拔插 USB 转串口线恢复。本次中断发生在加入检查点功能之前，没有保留整片备份。
- 导出工具已补上只读重试、过期应答过滤，以及每 4 KiB 落盘的 `.part` 检查点。完整文件只在读完后生成，`--resume` 可从匹配范围的检查点继续；断线后不能把 `.part` 当作完整备份。

## 后续离线改动（未刷入）

用户要求先不接触机器，以下仅通过主机模拟和双板构建验证：

- 板级 `BOARD_HasSi4732()` 声明：K1 不具备 SI4732，K6 保留改装探测。K1 即使 BK1080 探测失败，也不再尝试 SI；陈旧的 SI 计划、搜台、采样、RDS 与音频请求均被拒绝。
- 首启诊断页读取板级映射的原厂电池校准，验证六点严格递增且为非零 12-bit ADC 值后使用第 4 点；不写入配置，不覆盖已加载的用户校准。
- 导出工具采用上述限速、退避和连接总重试预算，并将主机检查点缩小到 4 KiB；测试覆盖过期应答、预算耗尽、禁止重试、断点续读和拒绝覆盖。
- 2026-10-19 更换 USB Hub 后，`/dev/cu.usbserial-21220` 一次 `info` 查询成功，容量仍为 2097152 B。旧 Hub 发烫与重试的因果关系未确定。此后按用户要求停止设备访问；整片双读仍未完成，外部 Flash 写入保持禁用。

下一步仍需核对 BK4829 接收寄存器差异；当前没有启用 K1 射频初始化或接收主界面。电池校准的新换算结果与接收支持都不能视为已经实机验证。

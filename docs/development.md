# 开发文档

更新于 2026-10-20。本文集中维护环境、源码职责、工具、资源预算、回归和硬件验收。产品规则见[设计文档](design.md)，操作见[用户手册](user-manual.md)，设备和地址证据见 [K6](hardware-uvk6.md)／[K1](hardware-uvk1.md) 硬件事实。历史审计与重构结果归入本文，不再以多个“当前状态”并列维护。

## 1. 环境与构建

需要 ARM GNU 工具链（arm-none-eabi-gcc 等）、Python 3 和打包依赖 crcmod；主机回归及 UI 预览需要 cc。K6 为 Cortex-M0／Thumb，应用 Flash 60 KiB、RAM 16 KiB，512 KiB EEPROM 独立预算；K1 为 Cortex-M0+，应用 Flash 118 KiB、RAM 16 KiB，2 MiB SPI NOR 独立预算。共享功能以 K6 预算为限。

```sh
git submodule update --init --recursive
python3 -m pip install crcmod
make BOARD=uvk6
make BOARD=uvk1
```

默认 release 使用 -Os、LTO、函数／数据独立段和链接垃圾回收。Makefile 收集 src/ 下 app（含 link）、ui（含 screens）、radio、store、core、devices、选定的 drivers 与 boards 源文件，不再靠旧应用排除清单。GIT_HASH 和 UTC 构建时间编入镜像。

| 选项／目标 | 行为 |
| --- | --- |
| make／make all | 默认 release；ENABLE_TX=1；K6 ENABLE_SI4732=1；FC 与频谱始终包含 |
| make -B -j4 all | 强制重编，测预算或改变编译选项时使用 |
| make -B -j4 all ENABLE_TX=0 | 仅接收，TX HAL 只保留关 PA 路径 |
| make ENABLE_SI4732=0 | K6 改用 BK1080 广播（原装板）；不编入 SI4732、RDS 与 SSB 路径。K1 的 board.mk 固定为 0 |
| make debug | 调试构建；不作为 release 容量基线 |
| make release | 清理、构建，并生成带时间戳镜像；K6 为 packed，K1 为 raw |
| make info／make help | 构建信息／目标说明 |

Makefile 跟踪 ENABLE_TX／ENABLE_SI4732／BUILD_TYPE，切换时删除该板旧对象；改工具链、CFLAGS、RADIO_CHIP 等其他参数仍须 `-B` 强制重编。可用独立 OBJ_DIR／BIN_DIR 做对照构建。两块板目录独立，可分别构建，不能共用镜像。

| 产物 | 用途 |
| --- | --- |
| bin/<board>/firmware | ELF，符号／段分析 |
| bin/<board>/firmware.bin | 原始应用镜像，仓库刷机脚本使用 |
| bin/uvk6/firmware.packed.bin | 经 tools/fw-pack.py 打包，供要求该格式的 K6 工具使用；K1 不生成 |
| obj/<board>/output.map | 实际链接布局与大小 |

`<board>` 为 `BOARD`，默认 uvk6。

raw 与 packed 不能互换。链接脚本不能为了通过构建扩大 Flash 上限。CI 在 [.github/workflows/c-cpp.yml](../.github/workflows/c-cpp.yml) 使用 Ubuntu 24.04，通过固定提交的 arm-none-eabi-gcc-action 安装 ARM GNU 15.3.Rel1（与本机预算基线一致），单独运行主机回归和菜单预览，再构建三种矩阵：K1＋BK1080、K6＋BK1080、K6＋SI4732。每种均检查仅接收（ENABLE_TX=0）变体，再构建默认 release；所有镜像始终包含 FC 与频谱；K6 raw≤61,440 B、K1 raw≤120,832 B，并校验对应向量表。每个 Artifact 独立包含带板型／芯片名的 raw、ELF、Map 和 build-info.json（提交、工具链、开关、SHA-256）；K6 另含 packed，K1 不打包。涉及 HAL／驱动仍须双板构建与分别实机确认。不同工具链产生不同大小，不混作优化增量。原 GCC 13.2.rel1 在源码 `8b34704` 的 K6 SI4732 默认构建占 61,460 B，超出上限 20 B，因此 CI 升级工具链，不扩大链接预算。工具链发行来源见 [Arm GNU Toolchain](https://gitlab.arm.com/tooling/gnu-toolchains-for-arm/-/tree/releases/15.3.rel1)。

## 2. 刷机与恢复

两块板均经 K 头 UART 编程线刷写，关闭电源，按住 PTT 开机，然后：

```sh
make BOARD=uvk6 flash PORT=/dev/cu.usbserial-1420
make BOARD=uvk1 flash PORT=/dev/cu.usbserial-1420
# Linux 按实际设备路径替换
make flash PORT=/dev/ttyUSB0
```

PORT 必须显式指定；macOS 可用 `ls /dev/cu.*` 查找。命令先构建，再由 tools/flash.py 发送对应板 raw 镜像，不操作外部 EEPROM／SPI Flash。K1 应用 USB CDC 不能用于 Bootloader 刷机。脚本使用 Python 标准库，无额外串口依赖。

打开串口后最多等待 45 秒握手，等待期间每秒一个点，握手成功立即刷写；不是等满 45 秒才开始。38400 baud、256 B 块、每块等待应答，最多尝试 5 次。先检查镜像长度与向量表，限制应用分区；K6 要求 Bootloader 3.x（本机 3.00.02），K1 要求 7.x（本机 7.02.02）；不匹配不握手、不写入。K1 按四字节版本握手、16 位小端页号／页数，最多 472 页；应用从 0x08002800 起。K6 保留本机已验证握手。

失败或中断时重新关闭电源，按住 PTT 开机，完整重刷。完成表示所有块收到 Bootloader 确认，不表示做过 Flash 回读验证；随后正常重启。首次配置初始化和 EEPROM 备份见用户手册。

```sh
python3 tools/flash.py --port /dev/ttyUSB0 --firmware bin/uvk6/firmware.bin
python3 tools/flash.py --board uvk1 --port /dev/ttyUSB0 --firmware bin/uvk1/firmware.bin
python3 tests/flash-regression.py
```

最后一条使用模拟串口，不连接设备。

## 3. 源码分层与公共 API

```text
app/             入口与编排：main.c、主循环 loop.c、主循环中的偏好应用、PC 链路 link/
ui/              界面：顶层为纯绘图、字体、菜单和输入工具；screens/ 为页面
radio/           收发信机行为：Slot 控制器、接收镜像、RX/TX 运行时、RDS、测量
store/           持久化：配置、频道、保存调度、资源、校准布局
core/            纯模型：不依赖 hal、驱动和其他目录，主机可直接测试
devices/         与板无关的整机设备：收发适配、显示、背光、音频、LED、电池、按键定义
hal/             只放硬件接口头文件，由板级与 MCU 驱动实现
drivers/<chip>/  芯片驱动平铺；含 mcu.mk 的目录是 MCU（寄存器、启动、时钟、外设）
boards/<board>/  引脚表、初始化顺序、机制不同的小函数、存储布局、链接脚本、board.mk
```

依赖方向：app → ui → radio → devices → drivers → hal；radio、devices 可读 store；core 不依赖任何目录；boards 实现 hal，可使用 drivers。`tools/check-layers.py` 按此检查所有 include，由 tests/run.sh 和 CI 执行，违反即失败。共享代码不写 `#if BOARD`：差异是数据（引脚、接线）就放进板级表格，差异是机制（外设、介质）就由板级实现最小的函数。

ui/screens 经 radio/radio.h 发意图和读取 RadioSlotStatus；radio 不包含 ui，不知道菜单、输入框或 F 前缀。ui 顶层的绘图代码不包含 ui/screens 和 radio，不访问射频全局状态，只绘制传入视图，因此 UI 预览和卡片回归可只编译它们。core/ 内的模型不调用存储、HAL、显示或系统时钟，由调用者传输入／回调。

| 模块 | 责任与入口 |
| --- | --- |
| radio/radio.c | Slot／VFO-MR 解析、request／apply 事务、RDS、提示、RADIO_SlotStatus |
| radio/seek.c、capture.c、spectrum.c、tone-scan.c | 临时 RF 功能、采用与取消／恢复 |
| radio/watch.c | core/watch 回调、暂停恢复、广播后台与事件归属 |
| radio/tx.c | PTT 对象与回话、忙碌检查、TX 安全、Slot TX 设置 |
| radio/radio-internal.h | 控制器模块间私有状态，页面不得依赖 |
| ui/screens/screen.c | 最多 6 层静态页面栈，栈底不可弹出 |
| ui/screens/home.c、slot-menu.c | 卡片、快捷键、VFO 频率／MR 频道号草稿；Slot 参数、独立 TX 数值草稿 |
| ui/screens/save-channel.c、channels.c、text-input.c | 编号／覆盖／名称保存、列表改名删除、字符输入 |
| ui/screens/settings.c、preferences.c、bands.c、reset.c | 全局偏好、预设草稿、初始化确认 |
| ui/screens/statusline.c | 所有页面共用的标题、F、写入、SI、锁、电池图标 |
| radio/receiver.c | gReceiver 统一硬件方案、测量和音频镜像 |
| radio/rx-runtime.c、tx-runtime.c | 接收失败回滚／TX 半双工生命周期 |
| core/radio-state.c、tuning.c | 保存状态、来源解析、跨频段步进、精确方案 |
| store/device-config.c、config-store.c | dirty／重试、UVR2 固定字节格式、双副本 |
| store/channel-store.c | 频道双副本（写旧份后镜像）、占用位图、CRC、删除 |
| boards/<board>/storage.c、storage-layout.h、calibration.c | 区域到物理地址、写保护、介质策略与板级校准；K1 含配置日志和频道扇区改写日志 |
| devices/rx-device.c | 芯片单位／带宽／信号口径／亚音、音频门、Seek／FC 适配 |
| devices/tx-device.c | 芯片调谐范围、校准读取与插值、物理 PTT／供电、PA 顺序 |
| drivers/bk48x9/ | BK4819／BK4829 共用代码（common、rx、tx、css、bus、capture）＋按 `RADIO_CHIP` 只链接一个芯片后端 bk4819.c／bk4829.c |
| drivers/{si473x,bk1080,st7565,eeprom-24cxx}/ | 外部芯片命令 |
| drivers/dp32g030/、py32f071/ | 各 MCU 寄存器、启动、时钟、定时器、SPI、UART、ADC、CRC；PY32F071 另有 USB CDC |
| app/link/uart-link.c、uart-protocol.c | PC 帧格式、混淆、CRC 与命令；经 hal/serial.h 选择请求线路，原始读／维护写交给板级 |

driver 不读取产品配置或实现菜单；devices 集中跨芯片适配，上层不依赖芯片单位、增益索引或寄存器号。

构建按板选择：`make BOARD=uvk6`（默认）读取 boards/uvk6/board.mk 得到 `MCU=dp32g030`，再引入 drivers/dp32g030/mcu.mk（CPU 与 CMSIS 选项），编译该板和该 MCU 目录下的源文件，产物在 bin/uvk6/、obj/uvk6/。MCU 驱动不认识具体引脚，板级不重复实现外设驱动；同一 MCU 的新机型只需新增 boards/ 目录。

芯片在构建时确定，运行时不探测型号：board.mk 用 `RADIO_CHIP`（bk4819／bk4829）选 BK 后端，用 `ENABLE_SI4732` 二选一广播芯片（1 = SI4732，0 = BK1080）。这些功能开关或 BUILD_TYPE 变化时，Makefile 会删除 obj/<board>/ 下的旧目标文件再编译，不用手动 `-B`。主机回归按后端和广播芯片分别编译；`bk48x9-trace-regression` 以拆分前驱动的完整寄存器写入序列为基线。

只有 boards/ 与 MCU 驱动目录可以包含 MCU 寄存器头。其余代码经 hal 接口访问硬件：单根信号经 hal/gpio.h 的 `GpioLine`（`GPIO_Write`／`GPIO_Read`／`GPIO_SetInput`，线路表由板级定义），显示总线经 hal/spi.h，背光输出经 `BOARD_BacklightSetLevel`，复位经 `SYS_Reset`，整机初始化经 `BOARD_Init`。

持久数据同理：store 与驱动只经 hal/storage.h 按“区域 + 区内偏移”读写（配置 A／B、频道 A／B、只读校准、只读资源区），不出现绝对地址。区域契约：断电时一次写入最多波及同一区域内的其他字节（闪存擦除单元），绝不影响其他区域；因此一条记录的两份必须放在不同区域，写完一份再写另一份。UV-K6 的 boards/uvk6/storage.c 把各区映射回原有 EEPROM 地址（频道两份仍交错存放），布局只写在 boards/uvk6/storage-layout.h，并由 storage-regression 校验。UART 原始读和维护写经 `BOARD_RawRead`／`BOARD_MaintenanceWrite`，按该板介质的绝对地址；24Cxx 驱动本身不含布局策略。

selectedSlot 是操作对象，receivingSlot 是接收归属，txSlot 可是回话对象，gReceiver.plan 是实际硬件镜像，runtime.current 是接收事务的运行方案。临时调谐不得把硬件镜像回写用户记忆。MR 的有效参数只读，保存频道是独立显式动作。

主循环每轮先 RADIO_TxSafetyUpdate，再保存调度与 RADIO_Update，最后页面更新／绘制。PTT release 总是优先处理，单项接收的普通静噪轮询在守听拥有音频时禁用。离开主屏会取消草稿、Seek、FC 与 PTT，接收和守听继续。

## 4. 文本、字体和开发工具

### 文本与字库

固件不用 printf。core/text 提供有界 TEXT_Begin／Add／Char／Number／Copy，支持十进制／八进制补零、自动截断；Print* 和 STATUSLINE_SetText 接收成品字符串，不是可变参数。短提示尽量不超过 19 字符，避免横条裁剪关键信息。

| 字库 | 当前用途 |
| --- | --- |
| ui/fonts/TomThumb.h | 小字，约 3×5；状态栏、横条、单位和菜单值 |
| ui/fonts/muMatrix8ptRegular.h | 中字，约 5×7；菜单项目、文本输入、卡片正文 |
| ui/fonts/symbols.h | 状态图标，8×8 内 |

卡片频率与名称都使用 UI_LargeTextWindow：中字×2，每个点向右多画 1 px，加粗但不引入另一套字库。数字步进 12 px、小数点 4 px，宽度计算与绘制一致。Helvetica 和数字专用字体已移除。

字形存 3 B 位域度量（宽、高、advance、x/y offset），不存 bitmapOffset；位图按每字形 ceil(width×height/8) 连续排列，读取时累加偏移。Cortex-M0 对 packed 数据的读取须检查生成指令，不能隐式使用未对齐半字访问。修改任何字形时验证全部字符范围和位图长度。

```sh
python3 tools/bdf-to-gfx.py input.bdf output.h --name myFont --line 8
```

muMatrix 源文件注明由 muwerk 的 mufont 生成；转换格式不意味着来源或授权信息可以删除。未来外部中文字库的安装、缓存与编码方案见设计文档。

### EEPROM 资源与校准工具

本小节写入命令仅适用于 K6。tools/resources.py 通过正常运行固件（非 Bootloader）的 UART 维护 EEPROM；K1 不支持校准／资源维护写，读取必须使用本板物理地址，见下一小节。固件命令 0x0570 只接受 SSB 资源尾区和校准区 `0x1E00–0x1FFF`，写后回读再应答；配置和频道仍不可由串口写入。

补丁不在固件中，安装到 EEPROM 尾部，无需构建 CRC 宏。当前资源为 `resources/si4735/patch_init.h`，8840 B，CRC32 为 `0xf9c0f474`；数据位于 `0x7DD78–0x7FFFF`，前方 16 B 为 `SSBP` 资源头（长度、CRC）。工具先清除资源头、写入数据，最后写头并全量回读，成功后重启刷新能力检测。`RESOURCE_SsbPatchPresent` 校验资源头和全部数据，驱动只读加载到 SI；不能只检查前缀，也不自动迁移其他固件的补丁布局。

```sh
python3 tools/resources.py --port /dev/cu.usbserial-1410 ssb-write
python3 tools/resources.py --port /dev/cu.usbserial-1410 ssb-verify
python3 tools/resources.py --port /dev/cu.usbserial-1410 read 0x1E00 0x200 cal.bin
```

K6 的维护写不适用于 K1；K1 USB／UART 只读诊断和备份方法见下文。

ssb-write 在尾区非空时先把原内容存到 backups/（已被 .gitignore 忽略），内容相同则不写。read 可保存任意地址范围，每次最多按 128 B 读取。

安装时使用普通开机状态，非 Bootloader；About → SSB patch 应显示 Present。SSB 可作为 SI 常驻源与 BK 守听并行，音频切换不重载 patch；SSB Seek 仍不支持。

校准命令都先把整个 0x1E00–0x1FFF 存到 backups/calibration-时间.bin，同一秒内的备份另起文件名不覆盖：

```sh
python3 tools/resources.py --port /dev/cu.usbserial-1410 cal-read
python3 tools/resources.py --port /dev/cu.usbserial-1410 cal-write 5 '5a5a5a 646464 7b7b7a'
python3 tools/resources.py --port /dev/cu.usbserial-1410 cal-seed
python3 tools/resources.py --port /dev/cu.usbserial-1410 cal-restore backups/uvk6/original-20241024/quansheng-k6.calibration.bin.bak
```

- cal-read：备份并列出 band 0–6 的 Low／Medium／High 各三个锚点。
- cal-write BAND ROW：写一行 9 字节；值不得为 00／FF（固件视为无校准）。TX 按频率使用 band 0–6 的全部记录，锚点和端点钳位规则见[两板 PA 规则](design.md#板级范围与-pa-数据)。
- cal-seed：只给全 00／FF 的 band 2／5 写参考值，已有数据不动。参考值来自三份公开 UV-K5(8) dump，High 取逐锚点最小值，**不是本机测量值**。
- cal-restore FILE：从 512 B 备份恢复整个校准区，只写有差异的 16 B 行，最后整块回读比对。全 00／FF 或 band 2／5 PA 行为空的文件直接拒绝，不访问设备。

写入前列出旧值和新值，须输入 yes 确认（--yes 跳过），写后回读。TX 每次发射时读 PA 表，写入后无需重启。参考值或手工值都不能代替功率计验收。

### 只读备份与诊断

正常开机（非 Bootloader）后可经 K 头 UART 读取两块板；K1 还可直接连接 USB-C 数据线，macOS 端口为 `/dev/cu.usbmodem…`，Linux 通常为 `/dev/ttyACM0`，以实际枚举为准。两条线路使用相同 PC 协议，USB 不暂停按键，K 头 UART 通信后按键暂停 5 秒；刷机仍走 K 头线。

```sh
# PORT 换成实际应用端口；先用 info 核对板型和容量
python3 tools/resources.py --port PORT info
python3 tools/resources.py --port PORT stats
python3 tools/resources.py --port PORT rf

# K6：512 KiB 物理 EEPROM 和校准区
python3 tools/resources.py --port PORT read 0 0x80000 k6-eeprom.bin
python3 tools/resources.py --port PORT read 0x1E00 0x200 k6-calibration.bin

# K1：2 MiB 物理 SPI NOR；与原厂 64 KiB 逻辑视图不同
python3 tools/resources.py --port PORT read 0 0x200000 k1-spi-read1.bin
python3 tools/resources.py --port PORT read 0 0x200000 k1-spi-read2.bin
python3 tools/resources.py --port PORT read 0x010000 0x200 k1-calibration.bin
cmp k1-spi-read1.bin k1-spi-read2.bin
shasum -a 256 k1-spi-read1.bin k1-spi-read2.bin
```

完整 K1 输出应为 2,097,152 B。两次期间保持配置稳定，若不同先定位差异，不能直接视为读取错误或宣布一致；本机两份备份仅差一次配置日志保存，记录见[K1 硬件事实](hardware-uvk1.md#整片备份2026-10-20)。`read` 每次最多 128 B、默认间隔 20 ms、超时 1 s、每块最多重试 2 次、连接总重试最多 6 次、退避 250 ms；`--no-retry` 禁用重试。达到限制停止，不自动重连或复位设备。

工具每 4 KiB 保存主机 `.part` 与范围检查点，读完才生成完整文件，拒绝覆盖完整输出。中断后对原命令追加 `--resume`，必须保持地址、长度和文件名一致；`.part` 不是完整备份。备份应异地保存，`backups/` 不入库，不能从公开仓库取得本机数据。

`info` 经 `0x0572/0573` 报告物理容量和存储名，`stats` 经 `0x0574/0575` 读取运行计数，`rf` 经 `0x0576/0577` 读取有时间戳的寄存器白名单。`rf` 不读取会消费事件的 REG02／FIFO；REG30 显示请求值缓存，其余逐个读取，并非同时采样。没有任意寄存器写入口，诊断不证明 RF 测量通过。

### UI 预览

```sh
python3 tools/ui-preview.py
python3 tools/ui-preview.py --frames frequency name --output /tmp/vfo-preview.png --scale 4
python3 tools/ui-preview.py --frames max maxtext --output /tmp/vfo-limits.png
python3 tools/ui-preview.py --frames multi-ab multi-cd --output /tmp/multi-vfo.png
```

脚本在临时目录用主机 cc 编译实际绘图代码，运行后清理；无需设备、ARM 工具链或额外 Python 包。菜单截图还编译实际 Slot／Settings 页面、菜单引擎与偏好格式化器，由 tools/ui-menu-preview.c 提供确定的射频／存储数据和无硬件副作用的主机适配。默认输出 docs/images/vfo-states.png，scale 为 1–12，默认 4。示例来自 tools/ui-preview.c，不能当作实机收台截图。

| 场景 | 用途 |
| --- | --- |
| plain、frequency、name、dual、single | 单／双卡片、广播／频道名称 |
| tuned、channel、max、maxtext、lw | 频率／频道号输入草稿、最大频率、长名称、kHz |
| tx、seek、rds | 发射、搜台、实际解析器的模拟 RDS 数据 |
| ab-a、ab-b、multi-ab、multi-cd | 选择与分页 |
| watch-probe、watch-rx、error-short、error-long | 守听归属和短／长提示 |
| manual-frequency、manual-name | 手册主屏：额外小数、预设差异、广播 PS／MR 名称 |
| manual-watch、manual-tx | 手册活动：选中 A 而 B 接收、选中 B 发射 |
| manual-input、manual-channel | 手册输入：MHz 频率草稿与 CH 频道号草稿 |
| manual-slot-rx、manual-slot-tx | 实际 Slot 菜单的 RX／TX 参数 |
| manual-settings、manual-display | Settings 根菜单与 Display |
| manual-receive、manual-transmit | Receive／Transmit 实际菜单与偏好值 |

手册截图采用当前卡片绘图代码、示例数据和对应主屏幕标题，不是实机采样。重新生成：

```sh
python3 tools/ui-preview.py --frames manual-frequency manual-name --output docs/images/manual-main.png
python3 tools/ui-preview.py --frames manual-watch manual-tx --output docs/images/manual-activity.png
python3 tools/ui-preview.py --frames manual-input manual-channel --output docs/images/manual-input.png
python3 tools/ui-preview.py --frames manual-slot-rx manual-slot-tx --output docs/images/manual-slot-menu.png
python3 tools/ui-preview.py --frames manual-settings manual-display --output docs/images/manual-settings.png
python3 tools/ui-preview.py --frames manual-receive manual-transmit --output docs/images/manual-rx-tx-settings.png
```

### 其他工具与串口

- tools/fw-pack.py：Makefile 使用的打包器。
- tools/gains.py：生成 BK4819 增益表。
- tools/map_analyze.py：分析真实链接段，排除 discarded／debug，data 同时计入 Flash 镜像和 RAM。
- tools/hawk5.py：历史 CHIRP 驱动，不能作为当前 UVR2／200 频道格式的读写工具。

应用 PC 链路可读取协议版本、板级物理存储及白名单 RF 诊断；原始存储写命令禁止，不提供任意寄存器写入。Bootloader hello 只提供其版本，不能得出 EEPROM 型号和容量。校准与补丁必须用可信读回信息判断。

## 5. Flash、RAM 与优化记录

2026-10-20 在 **fff1259** 基础上移除 FC／频谱编译开关后，双板默认 release 强制构建（`make -B -j4 BOARD=uvk6`／`uvk1`，ARM GNU 15.3.rel1，TX 开启，FC／Spectrum 始终包含，K6 SI4732、K1 BK1080）：

| 指标 | UV-K6 | UV-K1 |
| --- | ---: | ---: |
| Flash | **61,328 / 61,440 B** | **61,264 / 120,832 B** |
| Flash 余量 | **112 B** | **59,568 B** |
| 代码输入 | 54,528 B | 54,222 B |
| 只读输入 | 6,178 B | 6,389 B |
| data（RAM 初值，亦占 Flash） | 368 B | 388 B |
| bss | 6,152 B | 10,904 B |
| 静态 RAM（data＋bss） | **6,520 B** | **11,292 B** |
| 链接栈／堆预留（含对齐） | 256 B | 260 B |
| 链接 RAM 总占用 | **6,776 / 16,384 B** | **11,552 / 16,384 B** |

这是构建快照，不是运行栈峰值或最新 CI 声明。`size` 默认输出的 bss 包含链接栈／堆预留；静态 RAM 使用 `size -A` 的 data＋bss 或 map_analyze.py 结果，不能重复计入预留。K6 剩余空间不足以直接采用旧规划中的新功能体积估计；K1 较大的 Flash 预算不改变共享功能约束。以后只在本节更新资源基线，其他文档不复制随功能变化的大小。

链接后 Makefile 检查 ELF 符号：出现有符号 32 位除法（__aeabi_idiv／idivmod，约 460 B）、64 位除法、软浮点、printf 或 malloc 时构建失败并删除 ELF。最常见的来源是 uint8_t／uint16_t 被提升为 int 后再 / 或 %，写成 x % 60u 这类无符号运算即可；常量除数通常被编译成乘法，不一定触发。

LTO 的 Main、homeRender、applyPlan 等符号含内联子调用，不等于原文件的体积。字符串池合并后不能按名字归因某函数。分支数用于审视复杂度，Flash 优化看实际机器码和数据；拆文件、删已被 GC 剔除的源码不保证省空间。无符号或表驱动也不天然更小。

已完成的优化与审计收尾：

| 处理 | 结果／证据边界 |
| --- | --- |
| 删除旧扫描／旧频谱／Loot 与无引用 API（当前频谱为独立实现） | 源码更小，但已被 LTO 排除的部分不省 Flash |
| 统一接收状态、拆常驻控制器和页面 | 减少重复镜像，菜单不再暂停接收；单次布局变化须看实际构建 |
| 字形度量 8→7→5→3 B，删除额外大字体 | 复用中字绘制名称和数字；字形／卡片回归核对布局 |
| 移除 printf，改 core/text | 2026-10-14 当时独立对照净省 1,332 B，避免可变参数和 64 B 文本栈缓冲 |
| 提示及字符串统一 | 当时独立对照净省 492 B；不按重复源码长度推算收益 |
| 去有符号除法 | 处理负值映射后再替换；当前 ELF 不需要 __divsi3，不能直接将负数强转无符号 |
| 菜单完整枚举和断言 | 已消除旧测试裸数字索引导致的回归失败 |
| BK 非法 AGC 索引、BK1080 缓冲指针和未用项 | 已修复，Makefile 不再屏蔽不兼容指针及未用变量／函数警告 |

上述独立收益来自对应历史快照，不相加推导当前总量。保留 -Os、LTO、section GC；未采用表驱动的收益预测、fno-jump-tables 或删除 null 检查选项作为既定优化。以后对照构建记录代码版本、全部参数、工具链、功能组合、实际字节变化与行为核对，避免将历史估计写成当前事实。

```sh
make -B -j4 all
python3 tools/map_analyze.py obj/uvk6/output.map 20
arm-none-eabi-size -A bin/uvk6/firmware
arm-none-eabi-nm --size-sort -S bin/uvk6/firmware
```

未使用的字库文件不等于镜像资源；EEPROM 扩容不增加 MCU Flash／RAM。新功能优先去重复状态、恢复路径和格式转换，不能删 CRC、写保护、回读或 PA 关断来通过预算。

## 6. 回归与文档维护

```sh
sh tests/run.sh
```

tests/run.sh 构建主机用例和刷机模拟，无需对讲机。主机 Mock 验证逻辑和寄存器访问顺序，不证明真实音频、RF 或动态时延。

| 类别 | 用例 |
| --- | --- |
| 模型／事务 | model、tuning、rx-runtime、receiver、tx-regression |
| 控制器／页面 | radio-app、screen、menu、number-input、settings-inline、channels、band-editor |
| 存储／偏好 | config-store、channel-store、device-config、settings、backlight、uvk1-storage（真实 NOR 驱动＋掉电模型） |
| 接收芯片／亚音 | rx-device、bk-rx-mode、bk1080、si、tone |
| RDS／临时功能 | rds、seek、capture、bk-capture、watch、spectrum、tone-scan |
| TX | tx-device、tx-disabled；PA 最后开启、失败／松键／电压／TOT／恢复 |
| 字库／文本／协议 | font、radio-card、text、uart-protocol、flash-regression.py |

改字库／布局先核对全部字形与主界面、菜单预览。改模型／存储／TX 需覆盖边界、失败回滚和持久化；保持 UI 数据归属和物理 RX/TX 归属一致。处理时钟回绕用明确无符号 elapsed 或期限比较，不依赖普通大小比较。

文档集中维护五份：`user-manual.md` 讲当前操作；`design.md` 讲规则、数据模型与未实现规划；`development.md` 讲代码、工具、预算与验证；`hardware-uvk6.md`／`hardware-uvk1.md` 分板记录证据、规格、地址保护和实机结果。README 为项目入口，datasheets／resources 的 README 仅记录资料来源与资源说明。阶段专题已归入对应章节，不再并行维护旧“当前状态”；完整过程可查 Git 历史。

| 代码改变 | 必须同步核对 |
| --- | --- |
| ui/screens/home、slot-menu、settings、bands、channels、text-input | 手册的按键、菜单、输入和保存流程 |
| radio 与 core/tuning、watch、tx-model | 设计中的来源、调谐、音频归属和回话规则 |
| config-store、channel-store、板级 storage-layout | 设计中的字节格式、硬件事实中的地址与写保护 |
| Makefile、工具、目录或资源格式 | 开发文档；README 入口保持简单 |
| 新实机测量或设备变更 | 硬件证据与验收状态，区分用户确认／代码／厂家资料／实测 |

禁止在一份文档多次追加互相矛盾的“当前版本”。更新原章节；只有有价值的实测取舍保留明确快照。发布前检查 Markdown 本地路径与章节锚点、无旧入口、无未实现功能伪装、无过时存储格式或未校验预算。

### 接收与音频排查

2026-10-19 K1 最关键的接收缺陷是启动漏调 `RECEIVER_Init → RX_DEVICE_Init`：初始化寄存器未执行，未使用的调用链被 LTO 删除。补齐 `RADIO_Init` 入口后，真实寄存器和用户清晰语音均得到确认。主机检查应覆盖生产调用链，不能只单测驱动。校准读回和实际语音分别记录；REG3D 写入 `2AAB` 仍读 `0000` 的例外保留在[K1 硬件事实](hardware-uvk1.md#6-接收发射与实机证据)。

FM 周期声排查发现两处确定代码问题：守听音源逐主循环读取 RSQ、忽略采样失败并将其解释成静噪关闭。当前由 `RECEIVER_Poll` 和守听共用[采样策略](design.md#接收采样)，单次失败保留门控与 RSSI；回归覆盖短暂失败和守听读次数边界。SI RSQ／RDS 停止轮询的诊断构建开关在 K6 默认构建听音安静后已删除，不能继续写成受支持选项。

听音复现记录同一电台、信号、音量、天线、电源、扬声器，记周期与 RX 灯是否同步。可能的后续实验包括冻结 LCD 刷新、固定非 PWM 背光、在保留物理 PTT 安全的条件下暂停矩阵扫描、示波器查看电源／音频；未做实验前不能把引脚共享、RDS FIFO 阈值或其他设备的报告写成本机噪声原因。K6 保存图标与短暂断音的相关性尚待定位。实机结果分别归入两板硬件文档。

### 亚音识别与尾音验证

RX Tone Scan 的芯片配置为 REG51=`0300`；REG68 bit15 清零时取低 13 位 ×4843÷10000 得 CTCSS Hz×10；REG69／6A 低 12 位拼接 DCS，并重读 REG69 排除撕裂。扫描策略、三次独立采集、MR 只读及 DCS 等效码规则见[设计文档](design.md#独立-rx-tone-scan)。实现参考 [DualTachyon BK4819](https://github.com/DualTachyon/uv-k5-firmware/blob/main/driver/bk4819.c)、[F4HWN BK4829 5fedc09](https://github.com/armel/uv-k1-k5v3-firmware-custom/blob/5fedc09e9ddec350823adb9bb2d81df05b52ab8d/App/driver/bk4829.c) 和 [Egzumer DCS](https://github.com/egzumer/uv-k5-firmware-custom/blob/main/dcs.c)。

主机模型覆盖 50 个 CTCSS 及误差边界、104 个 DCS 正／反码 × 23 种循环起始位（4784 种波形）、单比特损坏、慢采集和非标准 CTCSS 不打断 DCS、失载波重置、PTT 排他、回话后恢复选中槽、VFO 单次提交、MR 拒写与恢复失败。优化后的 CTCSS 匹配还与旧算法逐值核对全部 65536 个输入；亚音识别独立于 FC，二者均始终编入。

TX tail 回归覆盖约 200 ms 非阻塞期限、计时回绕、连续松键、重按门禁、TOT／低压／取消硬停止、PA 顺序、普通 TX 清尾音模式、配置／频道 bit5 和仅接收构建。DCS 尾音 REG51 的芯片差异为 BK4819=`804A`、BK4829=`8040`；状态机和菜单共享。主机模型不能证明检测速度、灵敏度、尾音互通或波形。

## 7. 实机验收与未完成事项

已完成的用户听音、刷写和备份按日期与版本集中记录在 [K6](hardware-uvk6.md#7-接收听音与已知现象)／[K1](hardware-uvk1.md#实机记录与证据边界) 硬件事实。刷机 ACK 不等于 Flash 回读；寄存器请求值不等于实测输出；旧镜像验收不自动覆盖后续所有改动。

以下为仍需完成或补充的验收，不能写成全部通过：

- EEPROM／SPI：K6 bank／页／WP 细节与完整映射；配置单副本损坏、写入掉电与读回失败、频道删除中断；K1 真实掉电与日志重放。K1 已完成物理整片双读，写入范围仍限 `0x020000–0x049FFF`，原厂校准扇区绝不擦除。
- 接收：两板完整范围、AM、亚音灵敏度、静噪、240／280 MHz 前端分界、长时间听音与多次 FM↔NFM↔TX 恢复；基础 NFM／广播听音已确认，不能外推全范围。
- 守听：兼容组、三／四槽、去重、事件先到先得、2 秒回话、BK SQ0 连续独占、短呼叫漏检和菜单持续接收；K6 SI 常驻＋BK 事件与 K1 FM／NFM 双槽切换已有用户确认。
- Seek／FC／Spectrum／RDS：空场、强信号／谐波、取消和跨芯片恢复、RDS 标签与 CT 对时／过期；SSB 须校验补丁长度与全量 CRC。
- RX Tone Scan：两板分别测无亚音、多个 CTCSS、DCS N／I 等效项、弱信号、短时发射与重复扫描。当前只有代码和主机验证，K1 含该功能镜像的 ACK 不算识别验收。
- 故障：I²C NACK／时钟拉伸、UART 活动、锁键和物理释放；最长主循环阻塞、运行栈峰值与 PA 最坏关断时延。
- TX：假负载上功率、载频误差、调制频偏、杂散、带外抑制、各 PA 档和 Shift／Split；RX Tone 与 TX Tone 分开；无温度保护和独立硬件 PA 看门狗。K1 首版 TX 用户确认正常，仍缺仪表测量。
- TX tail：假负载测结束信令和载波延长，K6↔K1 及其他品牌接收机，CTCSS／DCS N／I、开关两态、漏字与拖尾；RX 尾音静音未实现。

## 8. 公开发布准备

项目代码采用根目录 [LICENSE](../LICENSE) 的 **GNU AGPL v3**。保留原作者声明，第三方代码、字体、厂商头文件、芯片资料和独立资源按各自授权与来源记录处理。README 的 Credits 汇总上游和主要参考，硬件文档保留具体版本及证据，不能将“参考过”写成原作者为本项目做了硬件验收。

发布前仍需补齐第三方授权清单：Hawk5 继承代码的授权依据、muMatrix／TomThumb 字体、PY32F071 厂商头文件与 CMSIS 子模块声明。仓库新增 AGPL 文本不替代这些材料。SSB 补丁原文件明确不属于 PU2CLR 库的许可证范围，保留原文件和[资源来源说明](../resources/README.md)，不要将它一并标作 AGPL。当前不宣称这些清单已经核对完成。

公开产物应注明板型、代码版本、工具链和功能开关，K6 共享功能保持 60 KiB 限制，K1 使用 raw 镜像。分别记录主机回归、双板构建、刷机 ACK、回读和实机验收；未经实测的项目保留待验证状态。备份、设备 UID、个人端口和本地日志不作为使用前置依赖；`backups/` 保持忽略，不上传本机 EEPROM／SPI dump 或原厂校准。

本次文档归档关系：

| 原专题 | 维护位置 |
| --- | --- |
| uvk1-bringup、uvk1-rx-audit、uvk1-tx-audit | K1 硬件证据；本文构建、备份、排查与验收 |
| listening-paths | 设计第 4 章；手册第 7 章 |
| fm-audio-noise | 设计采样策略；本文音频排查；两板听音证据 |
| tx-frequency-range | 设计范围／校准规则；两板硬件锚点；手册发射与提示 |
| rx-tone-scan、tail-elimination | 设计识别／TX tail 与未实现 RX 规划；手册操作；本文验证边界 |

旧专题不再单独保留，历史构建大小、阶段镜像及排查过程可从 Git 历史追溯。资料 PDF、原始资源及来源 README 保留。

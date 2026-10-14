# 开发文档

更新于 2026-10-14。本文集中维护环境、源码职责、工具、资源预算、回归和硬件验收。产品规则见[设计文档](design.md)，操作见[用户手册](user-manual.md)，固定设备和地址证据见[硬件事实](hardware.md)。历史审计与重构结果归入本文，不再以多个“当前状态”并列维护。

## 1. 环境与构建

需要 ARM GNU 工具链（arm-none-eabi-gcc 等）、Python 3 和打包依赖 crcmod；主机回归及 UI 预览需要 cc。当前平台为 Cortex-M0／Thumb，应用 Flash 60 KiB、RAM 16 KiB，512 KiB EEPROM 独立预算。

```sh
git submodule update --init --recursive
python3 -m pip install crcmod
make
```

默认 release 使用 -Os、LTO、函数／数据独立段和链接垃圾回收。Makefile 收集 core、hal、drivers、ui、radio、screens 和 system 的源文件，不再靠旧应用排除清单。GIT_HASH 和 UTC 构建时间编入镜像。

| 选项／目标 | 行为 |
| --- | --- |
| make／make all | 默认 release；ENABLE_TX=1、ENABLE_FC=1 |
| make -B -j4 all | 强制重编，测预算或改变编译选项时使用 |
| make -B -j4 all ENABLE_TX=0 | 仅接收，TX HAL 只保留关 PA 路径 |
| make -B -j4 all ENABLE_FC=0 | 去掉 FC，F→4 提示 Not supported |
| make debug | 调试构建；不作为 release 容量基线 |
| make release | 清理、构建，并生成带时间戳 packed 文件 |
| make info／make help | 构建信息／目标说明 |

不要仅改变量后依赖增量 make：编译参数变化不会自动使所有对象失效。可使用独立 OBJ_DIR／BIN_DIR 做对照构建，避免不同选项的对象混用。

| 产物 | 用途 |
| --- | --- |
| bin/firmware | ELF，符号／段分析 |
| bin/firmware.bin | 原始应用镜像，仓库刷机脚本使用 |
| bin/firmware.packed.bin | 经 tools/fw-pack.py 打包，供要求该格式的工具使用 |
| obj/output.map | 实际链接布局与大小 |

raw 与 packed 不能互换。链接脚本不能为了通过构建扩大 Flash 上限。CI 在 [.github/workflows/c-cpp.yml](../.github/workflows/c-cpp.yml) 固定 Ubuntu 24.04 的 ARM GNU 13.2.rel1 包，运行主机回归、无 FC／无 TX 变体和默认构建，检查 raw≤61,440 B，上传 ELF、两种镜像和 Map。不同工具链产生不同大小，不混作优化增量。

## 2. 刷机与恢复

标准 DP32G030 UV-K5/K6 Bootloader 串口刷写。关闭电源，按住 PTT 开机，然后：

```sh
make flash PORT=/dev/cu.usbserial-1420
# Linux 按实际设备路径替换
make flash PORT=/dev/ttyUSB0
```

PORT 必须显式指定；macOS 可用 `ls /dev/cu.*` 查找。命令先构建，再由 tools/flash.py 发送 raw 镜像，不操作 EEPROM。脚本使用 Python 标准库，无额外串口依赖。

打开串口后最多等待 45 秒握手，等待期间每秒一个点，握手成功立即刷写；不是等满 45 秒才开始。38400 baud、256 B 块、每块等待应答，最多尝试 5 次。先检查镜像长度与向量表，限制应用分区。

失败或中断时重新关闭电源，按住 PTT 开机，完整重刷。完成表示所有块收到 Bootloader 确认，不表示做过 Flash 回读验证；随后正常重启。首次配置初始化和 EEPROM 备份见用户手册。

```sh
python3 tools/flash.py --port /dev/ttyUSB0 --firmware bin/firmware.bin
python3 tests/flash-regression.py
```

第二条使用模拟串口，不连接设备。

## 3. 源码分层与公共 API

```text
main.c / hal/start.S  硬件与默认配置初始化
system.c             主循环、全局按键、显示节奏、UART、优先 TX 安全检查
screens/             页面栈、主屏、Slot 菜单、频道、设置、草稿输入
radio/               常驻控制器：Slot、来源、调谐事务、RDS、Seek/FC/Watch/PTT
ui/                  纯绘图、字体、菜单和输入工具
core/                模型、状态机、接收镜像、文本和持久化
hal/                 整机资源、总线及芯片适配
drivers/             按芯片组织的寄存器与命令实现
```

screens 经 radio/radio.h 发意图和读取 RadioSlotStatus；radio 不包含 screens／ui，不知道菜单、输入框或 F 前缀。ui 不访问射频全局状态，只绘制传入视图。纯模型 core/tuning、radio-model、watch、seek、capture 不调用 EEPROM、HAL、显示或系统时钟，由调用者传输入／回调。

| 模块 | 责任与入口 |
| --- | --- |
| radio/radio.c | Slot／VFO-MR 解析、request／apply 事务、RDS、提示、RADIO_SlotStatus |
| radio/seek.c、capture.c | 临时候选、采用与取消／恢复 |
| radio/watch.c | core/watch 回调、暂停恢复、广播后台与事件归属 |
| radio/tx.c | PTT 对象与回话、忙碌检查、TX 安全、Slot TX 设置 |
| radio/radio-internal.h | 控制器模块间私有状态，页面不得依赖 |
| screens/screen.c | 最多 6 层静态页面栈，栈底不可弹出 |
| screens/home.c、slot-menu.c | 卡片、快捷键、VFO 频率／MR 频道号草稿；Slot 参数、独立 TX 数值草稿 |
| screens/save-channel.c、channels.c、text-input.c | 编号／覆盖／名称保存、列表改名删除、字符输入 |
| screens/settings.c、bands.c、reset.c | 全局偏好、预设草稿、初始化确认 |
| screens/statusline.c | 所有页面共用的标题、F、写入、SI、锁、电池图标 |
| core/receiver.c | gReceiver 统一硬件方案、测量和音频镜像 |
| core/rx-runtime.c、tx-runtime.c | 接收失败回滚／TX 半双工生命周期 |
| core/radio-state.c、tuning.c | 保存状态、来源解析、跨频段步进、精确方案 |
| core/device-config.c、config-store.c | dirty／重试、UVR2 固定字节格式、双副本 |
| core/channel-store.c | 频道双副本、占用位图、CRC、删除 |
| hal/rx-device.c | 芯片单位／带宽／信号口径／亚音、音频门、Seek／FC 适配 |
| hal/tx-device.c | 板级范围、校准读取与插值、物理 PTT／供电、PA 顺序 |
| drivers/{bk4819,si473x,bk1080,st7565,dp32g030}/ | 芯片命令及外围寄存器 |

依赖只向下。HAL 抽象按键、屏幕、LED、音频、背光、EEPROM／总线等资源，也集中跨芯片适配；driver 不读取产品配置或实现菜单。上层不依赖芯片单位、增益索引或寄存器号。

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

### SSB 补丁安装与校验

补丁不在固件中，通过正常运行固件的 UART 安装到 EEPROM 尾部，无需构建 CRC 宏。当前资源为 `resources/si4735/patch_init.h`，8840 B，CRC32 为 `0xf9c0f474`；数据位于 `0x7DD78–0x7FFFF`，前方 16 B 为 `SSBP` 资源头（长度、CRC）。工具先清除资源头、写入数据，最后写头并全量回读，成功后重启刷新能力检测。`SETTINGS_IsPatchPresent` 校验资源头和全部数据，驱动只读加载到 SI；不能只检查前缀，也不自动迁移其他固件的补丁布局。

```sh
python3 tools/resources.py --port /dev/cu.usbserial-1410 ssb-write
python3 tools/resources.py --port /dev/cu.usbserial-1410 ssb-verify
```

安装时使用普通开机状态，非 Bootloader；About → SSB patch 应显示 Present。SSB 可作为 SI 常驻源与 BK 守听并行，音频切换不重载 patch；SSB Seek 仍不支持。

### UI 预览

```sh
python3 tools/ui-preview.py
python3 tools/ui-preview.py --frames frequency name --output /tmp/vfo-preview.png --scale 4
python3 tools/ui-preview.py --frames max maxtext --output /tmp/vfo-limits.png
python3 tools/ui-preview.py --frames multi-ab multi-cd --output /tmp/multi-vfo.png
```

脚本在临时目录用主机 cc 编译实际绘图代码，运行后清理；无需设备、ARM 工具链或额外 Python 包。默认输出 docs/images/vfo-states.png，scale 为 1–12，默认 4。示例来自 tools/ui-preview.c，不能当作实机收台截图。

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

手册截图采用当前卡片绘图代码、示例数据和对应主屏幕标题，不是实机采样。重新生成：

```sh
python3 tools/ui-preview.py --frames manual-frequency manual-name --output docs/images/manual-main.png
python3 tools/ui-preview.py --frames manual-watch manual-tx --output docs/images/manual-activity.png
python3 tools/ui-preview.py --frames manual-input manual-channel --output docs/images/manual-input.png
```

### 其他工具与串口

- tools/fw-pack.py：Makefile 使用的打包器。
- tools/gains.py：生成 BK4819 增益表。
- tools/map_analyze.py：分析真实链接段，排除 discarded／debug，data 同时计入 Flash 镜像和 RAM。
- tools/hawk5.py：历史 CHIRP 驱动，不能作为当前 UVR2／200 频道格式的读写工具。

应用 UART 可读取协议版本及限定 EEPROM 数据；原始 EEPROM 写命令禁止，UART 不是通用芯片探测／寄存器控制接口。Bootloader hello 只提供其版本，不能得出 EEPROM 型号和容量。校准与补丁必须用可信读回信息判断。

## 5. Flash、RAM 与优化记录

2026-10-14 在提交 **77a4274** 上强制构建默认 release（ARM GNU 15.3.rel1，ENABLE_TX=1、ENABLE_FC=1）：

| 指标 | 测量 |
| --- | ---: |
| Flash | **54,444 / 61,440 B**，余 **6,996 B** |
| 代码输入 | 47,994 B |
| 只读输入（含字体和字符串） | 5,837 B |
| RAM 初值 data | 372 B，亦占 Flash |
| 静态 RAM | 6,092 B |
| 链接栈／堆预留（含对齐） | 260 B |
| 链接 RAM 总占用 | **6,352 / 16,384 B** |

这是一次构建快照，不是运行栈峰值或最新 CI 声明。以后只在本节更新资源基线，用户手册与硬件事实不复制随功能变化的数字。源码或工具链变化须重新测量。

LTO 的 Main、homeRender、applyPlan 等符号含内联子调用，不等于原文件的体积。字符串池合并后不能按名字归因某函数。分支数用于审视复杂度，Flash 优化看实际机器码和数据；拆文件、删已被 GC 剔除的源码不保证省空间。无符号或表驱动也不天然更小。

已完成的优化与审计收尾：

| 处理 | 结果／证据边界 |
| --- | --- |
| 删除旧扫描／频谱／Loot 与无引用 API | 源码更小，但已被 LTO 排除的部分不省 Flash |
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
python3 tools/map_analyze.py obj/output.map 20
arm-none-eabi-size -A bin/firmware
arm-none-eabi-nm --size-sort -S bin/firmware
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
| 存储／偏好 | config-store、channel-store、device-config、settings、backlight |
| 接收芯片／亚音 | rx-device、bk-rx-mode、bk1080、si、tone |
| RDS／临时功能 | rds、seek、capture、bk-capture、watch |
| TX | tx-device、tx-disabled；PA 最后开启、失败／松键／电压／TOT／恢复 |
| 字库／文本／协议 | font、radio-card、text、uart-protocol、flash-regression.py |

改字库／布局先核对全部字形与主界面、菜单预览。改模型／存储／TX 需覆盖边界、失败回滚和持久化；保持 UI 数据归属和物理 RX/TX 归属一致。处理时钟回绕用明确无符号 elapsed 或期限比较，不依赖普通大小比较。

文档只维护四份：用户手册讲当前操作；设计文档讲规则与未实现规划；开发文档讲代码、工具、预算与验收；硬件事实讲证据、规格和地址保护。README 只保留简介和入口。

| 代码改变 | 必须同步核对 |
| --- | --- |
| screens/home、slot-menu、settings、bands、channels、text-input | 手册的按键、菜单、输入和保存流程 |
| radio 与 core/tuning、watch、tx-model | 设计中的来源、调谐、音频归属和回话规则 |
| config-store、channel-store、eeprom-layout | 设计中的字节格式、硬件事实中的地址与写保护 |
| Makefile、工具、目录或资源格式 | 开发文档；README 入口保持简单 |
| 新实机测量或设备变更 | 硬件证据与验收状态，区分用户确认／代码／厂家资料／实测 |

禁止在一份文档多次追加互相矛盾的“当前版本”。更新原章节；只有有价值的实测取舍保留明确快照。发布前检查 Markdown 本地路径与章节锚点、无旧入口、无未实现功能伪装、无过时存储格式或未校验预算。

## 7. 实机验收与未完成事项

固定设备型号和 512 KiB 总容量已经确认，不再次向用户核实。芯片数量、页大小和接线通过资料／代码／自动探测补证，ACK 候选不能当作物理鉴定。以下是验收计划，不表示已逐项通过。

- EEPROM：备份整个 512 KiB 和校准区，核对 bank／页／WP 映射；初始化前不写，初始化后仅配置双槽改变。验证单副本损坏、写入掉电与读回失败，频道删除中断单独检查。
- 接收：BK NFM／航空 AM、SI／BK1080 模式及音频切换、亚音和静噪、240／280 MHz 滤波；调谐失败恢复、同频缓存失效、未知芯片处理。
- 守听：广播＋事件、全部启用 Slot、事件先到先得、2 秒回复与 PTT 回话、BK 常驻断音、检测门槛与短呼叫漏检；菜单接收持续。
- Seek／FC／RDS：空场、强信号／谐波、短发射、取消和跨芯片恢复，RDS 归属与过期；SSB 要先核对 EEPROM 补丁位置和全量 CRC。
- 故障：I²C NACK／时钟拉伸失败及恢复，UART 活动、锁键和物理释放；测主循环最长阻塞和动态栈峰值，不把链接预留当最大栈。
- TX：假负载、功率／频偏／杂散、Low／Medium／High 校准、Mic gain／Deviation、Shift／Split、TX Tone；测松键、TOT、低电压及总线故障的最大 PA 关断时延。

### 特殊维护镜像

Makefile 另有 **SEED_PA_CAL=1**，强制 ENABLE_TX=0。启动时只对全 00／FF 的 VHF／UHF PA 九字节行写入参考值并读回；非空不覆写。它绕过正常配置／频道写区域，**不是日常初始化，也不是本机测得校准**。实现位于 hal/eeprom.c 和 main.c，地址和参考来源区别见硬件事实。普通构建 SEED_PA_CAL=0，不执行该操作。维护镜像不得作为默认发布镜像或偷偷用于设备容量探测。

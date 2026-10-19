# UV-K1 硬件事实

更新于 2026-10-19。本文记录用户新购的泉盛 UV-K1「MiniKong」。它**不是**本项目当前的固定开发设备；开发基线仍是 [UV-K6 512 大内存版](hardware-uvk6.md)。本文区分三类证据：**本机实测**（经 USB 只读读取）、**参考固件源码**（他人移植固件中的接线，未经本机 PCB 测量）和**厂家／社区资料**。

## 1. 本机实测（2026-10-19，正常开机模式，只读）

| 项目 | 结果 | 证据 |
| --- | --- | --- |
| USB 枚举 | 厂商 `QUANSHENG`，产品 `K1 CPS`，VID `0x36B7`、PID `0x0001`，bcdDevice 1.00，序列号 `2025080801` | macOS IORegistry |
| USB 类型 | 全速（12 Mbit/s）复合设备（bDeviceClass 0xEF/02/01），接口 CDC ACM（02/02/01），macOS 显示为 `/dev/cu.usbmodem20250808011` | IORegistry；**数据线直接接 MCU 的 USB**，不经过 USB 转串口芯片 |
| 原厂固件版本 | **7.02.07** | `0x0514` hello 的应答 `0x0515` |
| 串口协议 | 与 UV-K5 原厂相同：`AB CD` 帧头，16 字节 XOR 混淆表，CRC16/XMODEM，`DC BA` 帧尾；CDC 端口忽略波特率 | `tools/flash.py` 的帧函数可直接收发 |
| hello 附加字段 | AES key 标志 = 0，锁屏 = 0；版本字段 16 B 中 `7.02.07` 后紧跟 `63 7C 77 7B F2 6B`（AES S-box 首字节），说明原厂未清零缓冲区 | `0x0515` 原始应答 |
| EEPROM 读命令 | `0x051B` 支持 16 位地址，单次 128 B；`0x0000–0xFFFF` 两次读取逐字节相同 | 见 §4 |

本次没有发送任何写命令。

### Bootloader（2026-10-19，只监听）

K1 按住 PTT 开机进入刷机模式后，经 **K 头 UART 线**（`/dev/cu.usbserial-1420`，38400 baud）持续广播 `0x0518`，帧格式与 UV-K5/K6 相同（XOR 混淆、CRC16）。本次只接收，未回复 `0x0530` 握手，也未发送任何数据。

| 字段 | 值 |
| --- | --- |
| Bootloader 版本 | **7.02.02**（UV-K6 为 3.00.02） |
| UID（16 B） | `54 34 50 41 08 36 32 33 58 8e 80 00 78 d5 04 56` |
| 原始帧 | `1805 2000` + UID + `372e30322e3032000000` + `c4 20 00 08 00 00` |

参考固件的刷写工具（`tools/serialtool/_prog.py`）使用同一流程：收到 `0x0518` 后回 `0x0530`（Bootloader 版本），再以 `0x0519` 按 256 B 页写入，`0x051A` 应答。两种机型的 Bootloader 版本号前缀不同（3.x／7.x），刷写工具据此识别板型，避免把一种机型的镜像刷进另一种：参考项目记录了把 K1 镜像刷进 UV-K5 V1 后只能拆机经 SWD 恢复的案例。

## 2. MCU 与存储（参考固件源码）

主要依据：[armel/uv-k1-k5v3-firmware-custom](https://github.com/armel/uv-k1-k5v3-firmware-custom)（commit `5fedc09`，2026-10-17），即 F4HWN 的 UV-K1／UV-K5 V3 移植版；其 README 说明这两款机型改用 PY32F071。尚未通过本机芯片丝印核实。

| 项目 | UV-K1 | 本项目 UV-K6 基线 |
| --- | --- | --- |
| MCU | **Puya PY32F071xB**（Cortex-M0+） | DP32G030（Cortex-M0） |
| 内部 Flash | 128 KiB；Bootloader 占 `0x08000000–0x080027FF`（10 KiB），应用从 **`0x08002800`** 起，可用 **118 KiB** | 应用 60 KiB，从 `0x00000000` 起 |
| RAM | 16 KiB（`0x20000000`） | 16 KiB |
| 时钟 | 参考固件按 48 MHz 运行，HSE 宏为 24 MHz | DP32G030 平台配置 |
| 向量表 | 应用设置 `SCB->VTOR`（M0+ 支持） | 位于 0 地址 |
| 外部存储 | **PY25Q16 SPI NOR Flash，2 MiB**，扇区擦除 4 KiB，页编程 256 B；SPI2：SCK PA0、MOSI PA1、MISO PA2，CS PA3 | I²C EEPROM 512 KiB |
| 射频芯片 | 参考固件只编译 `bk4829.c`，即 **BK4829**（BK4819 的后续型号，寄存器大体相同但有差异） | BK4819 |
| 广播接收 | BK1080，独立软件 I²C：SCL PF5、SDA PF6 | BK1080 或 SI4732 改装 |

社区站点 [uv-k1.com](https://uv-k1.com/) 也说明换用了新处理器，UV-K5 固件不兼容；并提到 UV-K1(8) 有一个带 8 MB Flash 的型号。本机为 2 MiB 还是 8 MiB，需用 JEDEC ID（`0x9F`）确认；原厂固件没有提供这类读取命令。

## 3. 引脚（参考固件源码，未经本机通断测量）

依据：参考固件的 `App/board.c`、`App/driver/{gpio.h,st7565.c,py25q16.c,bk4829.c,i2c.c,uart.c,keyboard.c,backlight.c}`。

| 功能 | UV-K1（PY32F071） | UV-K6（DP32G030，见 [hardware-uvk6.md](hardware-uvk6.md#3-引脚键盘与物理标记)） |
| --- | --- | --- |
| LCD SPI | SPI1：SCK PA5、MOSI PA7；CS PB2、A0 PA6；参考驱动不操作复位脚 | SPI0：PB8／PB10，CS PB7，A0 PB9，RES PB11 |
| LCD 背光 | PF8；TIM7 + DMA 写 BSRR 做软件 PWM，4 kHz | PB6 |
| BK4829 三线总线 | CS PF9、SCL PB8、SDA PB9 | PC0／PC1／PC2 |
| BK1080 I²C | PF5／PF6（只用于 BK1080） | PA10／PA11，与键盘共用 |
| 键盘列（输出） | PB3–PB6 | PA10–PA13 |
| 键盘行（输入，上拉） | PB12–PB15 | PA3–PA6 |
| PTT | PB10，低电平为按下 | PC5 |
| 侧键 | 全部列为高电平时读取行线（零列 → SIDE1／SIDE2） | 同样方法 |
| 音频功放 | PA8 | PC4 |
| 手电／辅助 LED | PC13 | PC3 |
| 电池 ADC | PB0／PB1 模拟输入，参考固件读 ADC1 CH8 | PA9／PA14 |
| UART（K 型耳机口） | USART1：TX PA9、RX PA10 | UART1：PA7／PA8 |
| USB | MCU 自带 USB 全速，参考固件用 CherryUSB 实现 CDC | 无 |
| SWD | PA13／PA14（参考固件不启用 SWD 时改作 GPIO） | PB11／PB14 |

键盘逻辑矩阵（MENU/1/4/7、UP/2/5/8、DOWN/3/6/9、EXIT/*/0/F）与 K5 相同，只是端口不同。社区资料称 K1 用机身侧面的方向键代替 K5 正面的上下键；物理键帽布局尚未逐项核对。

## 4. 存储内容与本机备份

原厂 7.02.07 通过 `0x051B` 提供一个 64 KiB 的「兼容 EEPROM」逻辑视图。**它不等于 SPI Flash 的物理地址**：参考固件把逻辑校准区 `0x1E00–0x1FFF` 存放在物理地址 `0x010000`（4 KiB 扇区），读 `0x010000 + 0xC0` 即对应 `0x1EC0`。由于参考固件刷入后直接读取该扇区，可以推断原厂固件也把校准存在那里，但这一点未经本机物理读取证实。

| 逻辑范围 | 内容（本机读取） |
| --- | --- |
| `0x0000–0x00FF`、`0x0C80–0x0D5F` | 频道／VFO 频率记录（K5 格式，16 B 一条） |
| `0x0D60–0x0E2F` | 频道属性 |
| `0x0E70–0x0F4F` | 设置，含 `Quansheng` / `UV-K1` 开机字串、DTMF 等 |
| `0x0F50–0x10FF` | 频道名称 `CH001…` |
| `0x1C00–0x1C4F` | DTMF 联系人 `CALL1…` |
| **`0x1E00–0x1FFF`** | **原厂校准** |
| `0x2E00–0x31FF`、`0x3000–0x33FF` | 两份 `5A 5A BC 9A` 头加 128×64 位图，推测为开机画面 |
| 其余 | 全 `0xFF` |

### 校准数据要点（逻辑地址，布局同 UV-K5）

| 地址 | 本机值 | 说明 |
| --- | --- | --- |
| `0x1EC0` | `6E 00 78 00 82 00 8C 00 / B4 00 BE 00 C8 00 D2 00` | RSSI 门限（两组 4 × u16） |
| `0x1ED0 + band × 16` | band 0／1／3／6：`32 32 32 · 64 64 64 · 8C 8C 8C`；**band 2：`5F 5F 5F · 69 69 69 · 91 96 91`**；band 4：`5A 5A 5A · 64 64 64 · 96 96 96`；**band 5：`5A 5A 5A · 64 64 64 · 91 96 91`** | PA 偏置 L／M／H 三锚点 |
| `0x1F40` | 1442、2010、2163、2193、2314、2600 | 电池电压校准（u16） |
| `0x1F88` | 晶振 −2、`0x0046`、`0x0050`、音量增益 44、DAC 增益 14 | 杂项 |

K1 的 band 2／5 High 档（`91 96 91`）与本项目 `tools/resources.py` 中按 UV-K5(8) 推出的参考值（`7A 8B 7F`、`7B 7B 7A`）不同，说明功放链路有差异。**UV-K5／K6 的 PA 参考值不能用于 K1。**

### 备份文件

位于 `backups/uvk1/`（`.gitignore` 已忽略，只在本机，请另行异地保存）：

| 文件 | SHA-256 |
| --- | --- |
| `uvk1-7.02.07-calibration-1E00-1FFF-20261019.bin`（512 B） | `da3353f563615f969ce51e255618298738f1425119239bb324f0b5aa1b5f5d06` |
| `uvk1-7.02.07-uart-0000-FFFF-20261019.bin`（64 KiB 逻辑视图） | `6270405971bfa3f78d890af393298850d9a74388af180500292f38974b62ab7b` |

两次完整读取逐字节一致；单独读取的校准区与完整视图中的 `0x1E00–0x1FFF` 一致。

**MCU 内部固件没有备份。**原厂应用协议没有 Flash 读命令，Bootloader 协议也只能写入，只读方式无法导出。如需恢复原厂固件，可使用泉盛官网 [UV-K1 下载页](https://en.qsfj.com/support/downloads/8740) 提供的「CPS + 升级工具 + 固件」包（2026-06-08 版本）。2 MiB SPI Flash 中逻辑视图以外的内容，需要能按物理地址读取的固件才能导出（例如参考固件的 `0x0738` 命令）。

## 5. 当前项目能否直接运行

**不能。**当前 `bin/firmware.bin` 是 DP32G030 镜像：链接地址为 0、外设寄存器和全部引脚都不同，刷入 K1 无法启动。镜像大小（text 57,644 B，RAM 约 6.6 KiB 静态）在 K1 的 118 KiB／16 KiB 预算内，**容量不是问题**；需要移植的是平台层。上层的 `core/`、`radio/`、`ui/` 基本不依赖芯片，可以复用。

需要的改动按依赖顺序：

1. **平台与构建**：新增 PY32F071 寄存器定义、启动文件、时钟初始化（48 MHz）和 `firmware-k1.ld`（`0x08002800`，118 KiB，设置 VTOR）；Makefile 按 `BOARD=k6|k1` 选择平台目录，`-mcpu=cortex-m0plus`。`tools/flash.py` 的向量表校验、`FLASH_SIZE`，以及 `fw-pack.py` 的打包格式（原厂包在 `0x2000` 插入 16 B 版本字段、128 B XOR 表、CRC16）都要按板型区分。
2. **驱动脱离 DP32G030**：`drivers/st7565`、`bk1080`、`si473x`、`bk4819` 直接 include `drivers/dp32g030/*.h` 并操作 `GPIOB->DATA`、`SPI0->WDR`。应改为通过 `hal/gpio`、`hal/spi` 的板级接口访问，再为两块板分别提供 `hal/board-*.c`。按 AGENTS.md 可以直接改 HAL API，不保留兼容包装。
3. **HAL 重写**：gpio／keyboard（新引脚）、spi（SPI1 给 LCD，SPI2 给 Flash）、i2c（PF5／PF6，不再与键盘共用）、adc（PB0／PB1）、backlight（PF8 + 定时器 PWM）、uart（USART1 PA9／PA10）、systick、crc、led、audio（PA8）。
4. **射频芯片**：BK4819 驱动需要加入 BK4829 变体，至少核对 AGC、REG_43、REG_47 以及 FSK 相关差异（参考固件 `bk4829.c` 中有说明）。TX 必须等 PA 校准接通并做实机测量后才能开放。
5. **存储层（改动最大）**：`hal/eeprom` 和 `eeprom-layout.h` 假设 I²C EEPROM：可按字节或页覆盖写，地址最大 512 KiB。K1 是 SPI NOR：写前必须整扇区（4 KiB）擦除，只能把 1 改写成 0。
   - 抽象出与介质无关的存储接口；K1 实现为扇区读-改-擦-写，或改为只追加的日志式记录。
   - `UVR2` 的两份配置镜像必须放在**不同的 4 KiB 扇区**，否则擦除一个扇区就会同时破坏两份，掉电恢复失效。频道区同理。
   - 校准扇区物理地址 `0x010000` 加入写保护：普通写入和维护写都**不得擦除这个扇区**；PA 维护写需要整扇区备份后再重写，风险高，建议 K1 首版不开放。
   - SSB 补丁尾区：K1 没有 SI4732，首版直接不提供该区。
6. **USB CDC**：数据线连接的是 MCU 的 USB，不是 UART。要让 `tools/resources.py` 这类工具通过同一根线工作，固件需要 USB 全速设备栈和 CDC ACM（参考固件用 CherryUSB），这部分会占用相当的 Flash 和 RAM；也可以首版只用 K 口 UART（PA9／PA10，需要额外的 USB 转串口线）。
7. **接收能力与菜单**：没有 SI4732，因此 AM、SSB 和 SI 路径的 WFM／RDS 不可用，广播 FM 只有 BK1080。`hal/rx-device` 的能力检测需要能报告这种组合，UI 和设计中的路由规则要适应这一点。整机频段和功放范围需要按 K1 资料和实测重新确定。
8. **工具与文档**：`tools/resources.py` 的 `CAPACITY`、校准地址、PA 参考值都按 UV-K6 写死，需要按板型参数化；开发文档的验收项要增加 K1 一节。

建议的首个里程碑：只做第 1–3 步，加上只读存储（读取校准，不写配置），点亮屏幕、扫描键盘、背光、电池读数，确认能通过官方升级工具刷入并正常启动；然后再接 BK4829 接收，最后做存储写入和 TX。

## 6. 待确认

- PCB 丝印：MCU、SPI Flash 型号和容量（2 MiB 还是 8 MiB）、射频芯片是否确为 BK4829、是否有 BK1080。
- 原厂固件逻辑地址与 SPI 物理地址的完整对应关系。
- 官方频段、发射范围、功率和电池规格（官网规格页为前端渲染，本次未取到；社区资料称接收 50–600 MHz、最大 5 W、电池 1400／2500 mAh，尚未核实）。

# PTT 发射链路

2026-10-13。当前实现已接通固定开发设备（官方绿色 UV-K6 512 KiB EEPROM、DP32G030）的 BK4819 NFM 语音发射 HAL。此前空 HAL／全部发射锁定的设计已由本文替代。编译和主机模拟测试不等于实机功率、频偏及杂散验收；本轮未刷机或实际发射。

## 功能与使用

- 当前 VFO 菜单 `TX Enable` 设为 On 后，按住 PTT 发射，松开恢复该 VFO 的完整接收方案。已有配置与初始化默认仍为 Off，不自动打开所有 VFO 的 TX Enable。
- 仅 NFM、BK4819 接收路径允许申请发射。WFM、AM、LSB、USB 和 SI4732／BK1080 路径拒绝，不自动变更模式。
- 整机发射策略为 `136 ≤ f < 174 MHz` 和 `400 ≤ f < 470 MHz`，目标频率须满足 10 Hz 栅格。使用两个明确的频率区间，不采用 BK 芯片的整个接收范围，也不允许两个区间之间的频率。
- `TX Frequency` 为 Same 或独立频率，范围检查和校准按实际 TX Hz 执行；RX 频率保持不变。`TX Tone` 支持 Off、CTCSS、DCS 正／反码。
- Low／Medium／High 使用对应的本机原厂校准档，`F → 6` 循环选择；某档记录缺失或读取失败时，该档拒绝执行。
- 保留忙碌禁止发射、锁键时的 PTT 锁和 TOT（默认 120 秒）。PTT 归属选中 VFO，不跟随双守听后台音频。发射期间停止调频、搜台、捕获、守听和 RDS 操作。

## 路径与模块职责

```text
PTT 按下
  → VFO：取消前缀、恢复选中项、必要时等待并检测频道忙碌
  → TX_MODEL_Plan：TX Enable、NFM、板级范围、功率校准、亚音
  → TX_RuntimePress：冻结 RX 方案、关闭 PA、静音 RX
  → TX_DEVICE_Prepare：只读校准、插值、配置频率／麦克风／滤波／亚音
  → TX_DEVICE_Enable：启动语音 DSP、设置 PA 偏置、复查 PTT／电压、最后打开 PA
  → TX：红灯、VFO TX／功率显示
PTT 松开／TOT／供电异常
  → 首先关闭 PA，再停止 TX DSP
  → 失效 BK 接收缓存、恢复完整 RX 方案
  → RX；恢复失败则 FAULT，保持静音
```

| 模块 | 职责 |
| --- | --- |
| `vfo/vfo.c` | 选中项、PTT／忙碌判定、输入互斥、系统级安全检查和提示 |
| `core/tx-model.*` | 用户意图、频率区间和可用校准档的纯规划；失败清空输出 |
| `core/tx-runtime.*` | 半双工转换、准备失败回滚、释放、TOT 和必须松键才能重试的锁存 |
| `hal/tx-device.*` | 固定板级策略、只读 EEPROM 校准、供电与物理 PTT、PA／音频／LED 顺序 |
| `drivers/bk4819/tx.c` | NFM 语音寄存器、麦克风、频偏、滤波与频率配置；不读取 EEPROM |
| `hal/rx-device.*` | TX 后使接收缓存失效，重新打开 RX_ENABLE 并恢复模式、频率、带宽、静噪与亚音 |

`TxCapabilities.available` 表示此固件有板级实现，`power_mask` 表示目标频率对应的可用校准档；两者不宣称实机 RF 已测量。`TX_DEVICE_Capabilities(hz)` 显式使用实际发射频率，支持异频时不借用接收频段的校准。`TxPolicy` 保存两个不相连的板级频率区间。System → TX status 的 Ready 表示软件范围与校准具备基础条件，不代替本次 PTT 的完整校验。

## 原厂校准的只读使用

参考 [DualTachyon radio.c](https://github.com/DualTachyon/uv-k5-firmware/blob/main/radio.c) 的功率表布局：`0x1ED0 + band × 16 + power × 3`，每档三个偏置锚点，功率档顺序 Low、Medium、High。锚点频率参考 [frequencies.c](https://github.com/DualTachyon/uv-k5-firmware/blob/main/frequencies.c)。这些资料说明软件布局，不证明本机校准内容已经实测。

| 板级频率范围 | 原厂 band | 本项目只读地址 | 三个锚点（MHz） |
| --- | ---: | --- | --- |
| VHF 136–174 | 2 | `0x1EF0–0x1EF8` | 136、150、173.99999 |
| UHF 400–470 | 5 | `0x1F20–0x1F28` | 400、435、469.99999 |

HAL 每次读取该频段的九个字节，独立检查每档三个锚点均非 0／0xFF；读取失败不使用默认偏置替代。按目标 TX 频率进行两段线性插值，允许下降曲线，不从其他设备复制数据。此检查只能排除空记录和读取失败，没有校准 CRC，也不能确认任意非空字节的真实性或输出功率。

不改变 EEPROM 写入白名单，不覆写 `0x1E00–0x1FFF` 校准区，也不覆盖 SSB patch。EEPROM 为 512 KiB 不改变原厂校准区的低地址布局。

## 关断与恢复

- 所有关闭路径的第一项硬件操作都是关闭 PA_ENABLE，然后清除 PA 偏置、红灯、TX DSP 和亚音。关闭可重复调用，空闲释放不把正常 RX DSP 关掉。
- 开 PA 之前检查物理 PC5 PTT 电平及电压；准备期间松键不产生 RF 输出。系统主循环也读取物理 PTT，避免关断只依赖队列中的释放事件。
- 电压当前要求 6.00–9.00 V；未知读数拒绝。发射中电压超界立即关闭并恢复 RX，必须松键才能再发射。阈值是当前软件边界，需实机验证。
- TOT 到期先关闭 PA 并恢复 RX；按住不自动重发。准备或启动失败同样需要新的 PTT 按下事件。
- 恢复先失效 BK 快速调谐和模式缓存，再启用 RX_ENABLE、完整应用 RX。即便 TX 与 RX 同频，也不能跳过配置恢复。
- BK 串行写入无可靠应答，HAL 成功只说明寄存器命令已执行。当前没有温度传感器保护或独立于主循环的 PA 关断看门狗；主机测试不替代 RF 输出、最长主循环阻塞和关断时延测量。

## 验证

- `tests/tx-device-regression.c`：实际板级 HAL 与语音配置，模拟 EEPROM／GPIO／寄存器；测试校准地址、每档有效性、插值、越界、读取失败、PA 最后开启、准备中松键、电压异常及幂等关闭。
- `tests/tx-regression.c`：双区间、独立 TX 频率、三个校准功率档、模式拒绝、失败清空计划、TOT、准备失败和恢复故障。
- `tests/vfo-regression.c`：PTT 归属、忙碌判定、菜单／输入／捕获互斥、物理释放、供电异常、TX 提示与恢复。
- `tests/rx-device-regression.c`：TX 缓存失效后同频恢复必须重新配置和打开 RX_ENABLE。

运行 `sh tests/run.sh` 和 `make -j4`。本轮没有刷机、读取本机校准或测量发射波形；实机验收需核对本机校准读取、PTT 松开与 TOT 关断、三个功率档、TX 亚音及独立 TX 频率后的 RX 恢复。

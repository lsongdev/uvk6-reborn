# UVK6 Reborn — TX 路径与半双工安全设计

> 状态：**单 VFO TX 配置和模拟半双工运行时已实现，真实 PA 发射仍未启用或验证**。本文规范未来代码如何设计，不构成任何频率的发射许可，也不意味着可以在真实天线上测试。接收设计见 [design.md](design.md)，硬件事实见 [hardware.md](hardware.md)。

## 1. 目标与首版边界

1. **收发规划独立，运行时互斥**。同一套 `RadioRuntime` 独占 BK4819、射频开关、PA、音频与当前工作状态。
2. **首版只考虑 BK4819 NFM 同频语音发射**，并从经校准的低功率设置开始；第一版不支持异频、AM、WFM、LSB/USB、VOX 自动触发、数字协议或未经验证的宽频发射。
3. SI4732 与 BK1080 **始终仅接收**。用户在 SI4732/WFM/SSB、BK1080/WFM、BK4819/AM 等模式下按 PTT 必须拒绝，不自动变更解调模式或选择 BK4819 兜底。
4. **整机 TX 能力不同于 BK4819 芯片的工作范围**。即使 RxPlan 由 BK4819 执行，若 PA、RF 滤波器、天线匹配、硬件型号及实测校准信息未明确支持，TxPlan 也必须拒绝。
5. 代码实现可以不兼容旧版 EEPROM，但不得覆盖 RF 校准数据或 SI4732 补丁区；真实 PA 使能需单独经过验证，不随此次文档改动开放。

## 2. 模型：用户意图、发射授权、硬件计划

### 2.1 类型关系

```text
用户频率 / RxConfig ──> ReceiverCapabilities ──> RxPlan
              │
              └──> TxConfig + TxCapabilities + TxPolicy
                                        │
                                        ▼
                              TxResult / TxPlan
                                        │
                                        ▼
                             RadioRuntime / TX HAL
```

- `RxConfig`：RX 模式、步进、带宽、静噪；**不承载发射授权**。当前代码的 `RadioParameters` 已移除旧 `tx_enabled` 字段。
- `TxConfig`：用户发射意图，例如显式启用、功率档；高级设置未来加入异频与发射信令。配置即使启用也不代表硬件允许发射。
- `TxCapabilities`：已确认的整机 TX 硬件特性（BK 路径、PA、输出滤波、可用功率级、校准有效性等）。
- `TxPolicy`：整机经过验证的发射频率白名单、可选区域约束以及保护限制；必须与用户的操作许可相符。用户不能靠修改频段名称、RX 模式或 `TxConfig.enabled` 扩大发射资格。
- `TxPlan`：本次通过所有校验的运行时发射计划，包含 `frequency_hz`、NFM、发射功率校准档、选定 PA/滤波路径、频偏等。硬件寄存器量由 HAL 根据校准生成，不写进通用用户配置。

下列为设计示例；当前可编译的精简版本见 [tx-model.h](../src/core/tx-model.h)，部分硬件字段仍待验证后加入：

```c
typedef enum { TX_POWER_LOW, TX_POWER_MEDIUM, TX_POWER_HIGH } TxPower;

typedef struct {
    bool enabled;
    TxPower power;
} TxConfig;

typedef struct {
    uint32_t frequency_hz; // 与 RX 分开，首版默认相同
    TxPower power;
    uint8_t rf_path;       // 来自受校验的硬件能力表
    uint16_t deviation_hz; // 来自已校准的模式策略，而非任意 UI 数值
} TxPlan;

TxResult RADIO_MODEL_PlanTx(const RxPlan *rx,
                            const TxConfig *request,
                            const TxCapabilities *hardware,
                            const TxPolicy *policy,
                            TxPlan *tx);
```

`TxPlan.frequency_hz` 与 RX 分离只是让未来异频中继无需重构公共接口，**首版必须要求与当前 RX 频率相等**。

### 2.2 校验顺序与错误

所有路径默认拒绝（fail closed）。PTT 请求按以下逻辑校验：

1. 已完成启动、当前 RxPlan 有效；没有 Seek、RDS 正在变更硬件、EEPROM 初始化、菜单编辑、硬件故障或别的互斥操作。
2. 用户确实请求启用 TX；RX 明确处于 NFM 且由 BK4819 接收；未来添加异频时，使用 TX 自己的已确认发射模式与频率。
3. 确认整机 PA/滤波/天线/校准能力，并检查目标频率位于**整机实测且符合使用条件**的 TX 白名单；不直接采用芯片的 18–660/840–1300 MHz 接收范围。
4. 检查 PTT 锁定、低电量、异常电压、温度/保护状态、看门狗、TOT 到时锁定等条件。
5. 全部成功才创建 TxPlan；拒绝时返回明确原因，如 `TX_DISABLED`、`TX_UNSUPPORTED_MODE`、`TX_OUT_OF_BAND`、`TX_LOW_BATTERY`、`TX_BUSY`、`TX_TIMEOUT` 或 `TX_HW_FAULT`。不应为实现方便自动改变模式或选择芯片。

软件白名单只是安全门槛，不是无线电操作许可的证明；用户仍需遵守当地法规和持有必要授权。

## 3. 单一半双工状态机

```text
RX
 └─ PTT down / valid TxPlan ──> TX_PREPARE ──> TX_ACTIVE
                                  │                │
                       any error  │                ├─ PTT up / TOT / fault
                                  ▼                ▼
                                FAULT <── RX_RESTORE
                                             │
                                  restore success
                                             ▼
                                             RX
```

状态机使用单调时间源及无 RF 的 TxRuntime，并由 VFO1 与 RxRuntime 共同持有；不允许并发的第二个射频控制者。相同时间只允许一个状态转换拥有 RF 驱动。需要有显式的 PTT 事件、未释放锁定标记和一次性的发射计划。

**TX_PREPARE（所有步骤 PA 均关闭）：**

1. 冻结已成功应用的完整 `RxPlan`；阻止新的 Seek、调频、RDS、静噪轮询及配置写 RF；清除 RX 音频开关。
2. 关闭扬声器/接收音频，静音必要的 SI4732/BK1080 路径；不要假定 SI4732 与 BK4819 共用音频或射频路径的实际电路。
3. 确认 TX HAL 的 PA 默认关闭；设置 BK4819 发射频率、PLL、NFM 调制、麦克风、发射滤波及经校准的低功率路径。
4. 校验 PTT 仍按下、所有安全条件仍有效且没有超时；再开启 TX 所需驱动，**最后才启用 PA**。如果底层寄存器写入无可靠应答，不能把“写命令已发出”冒充输出已验证。

**TX_ACTIVE：** 持续检查 PTT、TOT、供电及故障状态；在任何正常或异常退出路径首先关闭 PA。发射期间不允许修改频率、模式、功率、音频通道或重新开始 Seek。

**RX_RESTORE：** PA 已关闭后停止 TX DSP 与麦克风，恢复原 `RxPlan` 的硬件路径，再启动静噪、测量和音频；只有 RX 恢复成功才回到 `RX`。从 SI4732 的 AM/SSB 等路径恢复时，若发生断电，必须重新执行正确的初始化/补丁处理；不可假设开机配置仍存在。

**FAULT：** 强制 PA 关闭并保持关闭；接收恢复不成功时保持安全静音，向用户显示错误。PTT 长按或反复事件不能越过 fault latch。必要时只能在 PTT 释放、故障解除且重新校验后再次申请 TX。

### 3.1 PTT/TOT 的硬规则

- **释放优先**：PTT release 事件不能被菜单、按键锁、扫描或 UI 事件队列吞掉；所有状态都可以执行幂等的 `TX_DEVICE_Disable()`。
- **按住时间上限**：TOT 到期立即关闭 PA，进入 `TX_TIMEOUT` 锁定；需先释放 PTT 才能再次申请发射，避免重复长按立即重发。
- **启动安全**：系统复位、未初始化配置、异常电压以及看门狗恢复时 PA 初始关闭；软件未建立合法 TxPlan 前不得输出。
- **取消与故障**：发射准备失败，立即关闭 PA 并恢复 RX；硬件故障无法判断输出已停止时不继续尝试发射。

## 4. HAL 边界与可验证性

```c
bool TX_DEVICE_Prepare(const TxPlan *plan); // 不允许实际 RF 输出
bool TX_DEVICE_Enable(void);               // 合法状态下最后执行
void TX_DEVICE_Disable(void);              // 幂等，任何时候可调用
```

- HAL 负责 BK4819 的 TX DSP、麦克风路径、PLL、发射滤波、PA 与电源安全顺序；不能让 UI 直接写 BK/PA 寄存器。
- 射频输出功率不能由 UI 写裸寄存器；`Low/Medium/High` 是用户档位，具体 PA 参数由已确认的功放和校准表映射。标定未完成的档位不可选择。
- 需要独立的**硬件发射使能闸门**：未完成实机验证前 TX HAL 只允许无 RF 输出的模拟状态机与测试；真实 PA 使能不随 `TxConfig.enabled` 自动开放。
- 不新增 EEPROM 任意地址写接口；未来若引入发射校准写入，应使用受保护的专用维护路径，并先备份原厂校准区。
- 实机验证从已确认硬件、假负载、功率计和频谱分析开始，检查输出功率、谐波/杂散、温升、故障关断和 SI4732 → BK4819 → SI4732 的恢复。未经这些验证不以真实天线进行 TX 测试。

## 5. 已确定：仅一个 VFO，不设计频道

单 VFO 同时保存 RX 参数和一份独立的 TX 用户意图，不创建频道、MR、收藏夹或扫描组。频段仍只保存 RX 预设；切频段不会自动授予 TX 权限。

| 范围 | 当前配置 | 位置 |
| --- | --- | --- |
| VFO 接收 | 频率、模式、步进、带宽、静噪 | 原 VFO 参数菜单 |
| VFO 发射意图 | TX Enable（默认 Off）、TX Power（Low/Medium/High） | VFO 菜单滚动选择 |
| 全局安全设置 | TX timeout（默认 120 秒）、Busy lock（默认 On）、PTT Lock | Settings → TX Safety，另有 Lock PTT |
| 硬件授权及校准 | PA、滤波路径、已验证 TX 范围、实际功率 | 不可由用户扩大的 TxCapabilities/TxPolicy |

实现位于 `src/core/tx-model.[ch]`、`tx-runtime.[ch]` 和 `src/hal/tx-device.[ch]`。当前 `TxPlan` 仅能表示 **BK4819 + NFM + 同频 + 已标定 Low**；更高功率档允许保留用户选择，但未校准之前不得执行。当前板级 TX HAL 明确返回无有效发射能力，不调用 PA 或 BK TX 寄存器；PTT 只返回诸如 `TX HW locked` 的拒绝原因。

当前 TX 状态由 VFO1 以 `RxRuntime + TxRuntime` 一起管理，不引入第二个并发收发控制者。PTT release 由系统层优先处理，防止键盘锁和菜单吞掉必要的关断事件。TOT 检查也在系统主循环每次迭代优先运行，不仅依赖 VFO 页面更新；这仍不是独立硬件 watchdog，真实 PA 使能前必须测试最长阻塞时间及硬件关断保障。发射超时、恢复失败和故障锁存已由无 RF 主机模拟测试验证，**并非真实发射验收**。

### EEPROM

保留 `UVR2` 魔数，格式从 3 改成 **4**；双槽各 **512 字节**，地址 `0x0000–0x03FF`。记录中增加 TX Enabled、TX Power、TOT（秒）和 Busy Lockout；CRC、序号、写后回读不变。旧格式不迁移，用户需要明确初始化；原厂校准区 `0x1E00–0x1FFF` 与高地址 SSB patch 不写入。正常固件只允许操作这两个配置槽位。

### 后续，不在当前版本提供

异频偏移与 Split、RX/TX CTCSS/DCS、VOX、DTMF、Roger、STE 及真实可发射功率均暂缓。以后优先在当前 VFO 的独立 TX 配置中添加，不把一个中继设置硬塞进整个 2m/70cm 频段。真实 TX 开放之前需完成整机射频路径、校准、低电压/温度保护与假负载实测。

## 6. 测试和分阶段实现

1. **纯模型**：覆盖全部拒绝原因、权限交集、越界、模式不匹配、空校准、策略限制；RX-only 编译不可能给出可执行真实 TX 计划。
2. **无 RF 运行时**：模拟 PTT 按下/松开、抖动、重复按键、PTT 长按、TOT、低电压、Seek 期间按键、SI 断电恢复、RX 恢复失败、故障锁存。
3. **HAL 模拟**：用受控 mock 验证 PA 最后打开、最先关闭、异常时幂等关闭、配置失败绝不 Enable。
4. **硬件核实**：确认本机 TX 功放、滤波路径及寄存器，建立经验证的可用频段与校准表。
5. **受控实机验收**：先使用假负载和测量仪器完成输出功率、频偏、谐波、温升、TOT/PTT 急停及 RX 恢复验证。明确确认后，才可考虑提供用户可用的 TX 功能。

首版上线目标仍为 **BK4819 + NFM + 同频 + 已标定的 Low Power + PTT + TOT + RX 恢复**。以后通过独立的 TxConfig/TxPlan 频率，再扩展中继异频；不恢复旧的 TX/多 VFO 脏标记架构。

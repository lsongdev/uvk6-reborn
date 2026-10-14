# Radio 主屏幕架构

2026-10-14 重构后。射频由常驻的 Radio 控制器 `src/radio/` 运行，页面在 `src/screens/` 中以栈组织；控制器五个模块通过 `radio-internal.h` 共享少量状态，对页面只暴露 `radio/radio.h`。接收硬件只有一份状态 `core/receiver`。

## 概念

- **RadioSlot（A/B/C/D）**：独立工作位置，最多四项、每页显示两项。含各频段 VFO 记忆、当前来源、MR 频道编号与 TX 配置。两项及以上时守听全部已启用 Slot。
- **RadioSource**：`RADIO_SOURCE_VFO`（直接调谐）或 `RADIO_SOURCE_CHANNEL`（MR 频道）。
- **RxMode**：NFM、WFM、AM、LSB、USB，是解调模式，与来源无关。
- **RadioBand**：频段表（LW、MW、SW、FM Radio、Airband、2m、70cm 及 VHF/UHF 兜底），覆盖 150 kHz–1.34 GHz 全部频率，窄频段优先。
- **RxPlan**：设备＋频率（Hz）＋参数。由 `RADIO_MODEL_Plan` 根据频率、模式与已探测芯片生成，是上层与硬件之间唯一的接收描述。
- **Receiver（`gReceiver`）**：硬件此刻所调谐的 `RxPlan`、最近 RSSI 采样与音频开关。用户调谐、Seek、Watch、TX 恢复都只经 `RECEIVER_Apply` / `RECEIVER_Select` 改变它。
- **RxRuntime（`runtime`）**：用户已提交的接收方案，是失败或暂停后的恢复目标。

## 数据流

```text
RadioSlot.source
    ├── VFO: stored.current + stored.bands
    └── MR: CHANNEL_STORE_Load(channel_number)
                  ↓
         TUNING_Resolve（临时有效视图，无副作用）
                  ↓
    RADIO_MODEL_Plan / TUNING_ExactPlan → RxPlan（自动选芯片）
                  ↓
    RX_RuntimeTune（失败恢复上一方案）→ RECEIVER_Apply
                  ↓
    RX_DEVICE_Apply（BK4819 / SI4732 / BK1080 各自配置）
                  ↓
    RADIO_SlotStatus → screens/home → ui/radio-card
```

**连续调谐**：`TUNING_StepVfo` 按当前步进移动；跨入另一频段时采用该频段预设（模式、步进），`RADIO_MODEL_Plan` 随之切换芯片。上层不判断芯片。只有芯片分辨率把目标量化到其他频段时才提示 Band boundary；手动输入的量化差异需确认，已保存来源（MR、Slot 切换、切回 VFO、Watch）要求精确表示。

**调谐是事务**：先规划，再经 `RX_RuntimeTune` 应用；失败不提交 Slot、来源或保存。Seek/FC/Watch 临时接管硬件（改变 `gReceiver`），结束时恢复 `runtime.current`，不是第三种来源。

## 模块

控制器（`src/radio/`，不包含 `ui/` 或 `screens/`）：

| 模块 | 职责 | 对外 |
| --- | --- | --- |
| `radio.c` | Slot、VFO/MR 解析、调谐事务、RDS 标签、状态快照、提示 | `RADIO_TuneTo`/`Step`/`SwitchSlot`/`SlotStatus`/`Notice` 等 |
| `seek.c` | Seek 状态机：BK/BK1080 逐步检测，SI 用芯片 Seek，单次回绕 | `RADIO_StartSeek`/`CancelSeek`/`Seeking` |
| `capture.c` | FC：BK4819 频率捕获、自动/VHF/UHF、确认后直接调谐 | `RADIO_StartCapture`/`CaptureAdopt`/`CaptureCancel`/`CaptureNextScope` |
| `watch.c` | 多路守听：广播常驻源 + BK 事件轮询、先到先得；暂停时恢复主接收 | `RADIO_IsWatching`/`WatchStatus`/`Heard` |
| `tx.c` | PTT、忙碌禁发、电压/松键/TOT 安全、TX 设置 | `RADIO_PttPress`/`PttRelease`/`SetTx`/`CycleTxPower`/`TxSafetyUpdate` |

页面（`src/screens/`，经 `screen.h` 入栈出栈）：

| 页面 | 职责 |
| --- | --- |
| `home.c` | 栈底页：卡片与标题、键盘频率输入、F 组合键、调谐确认；按键翻译为 `RADIO_*` 意图 |
| `slot-menu.c` | Slot 菜单（项目枚举与 `RadioParameterField` 对齐），自带 TX 频率输入 |
| `save-channel.c` | 频道命名、保存与覆盖确认 |
| `settings.c`、`bands.c`、`channels.c` | 全局设置、频段预设、频道列表 |

页面私有的交互状态（输入草稿、F 前缀、菜单光标）只在页面中；离开主屏幕时 `RADIO_CancelInteractive()` 停止 FC、Seek 与 PTT，守听和接收继续。控制器内其余状态均为 `static`。主机回归 `tests/radio-app-regression.c` 以单一编译单元包含控制器与主屏幕相关页面，可直接检查私有状态；`tests/screen-regression.c` 覆盖页面栈。

## 硬件边界

`hal/rx-device` 是唯一包含芯片差异的模块：10 Hz 单位、带宽索引、BK 快速步进缓存、SI 模式切换、BK1080 静音、各芯片 RSSI/SNR 口径、BK 亚音判定、音频门时序（开：DAC→延时→喇叭；关：喇叭→延时→DAC）、Seek 与 FC。`core/receiver` 不包含任何芯片判断。

`selectedSlot` 表示用户操作对象，`receivingSlot` 表示当前物理接收归属；频率草稿属于主屏幕，输入期间按键全部交给草稿，不会串到其他 Slot。PTT 物理松开、电池电压和 TOT 由 `system.c` 主循环调用 `RADIO_TxSafetyUpdate()`，不依赖当前页面。

## 兼容与验证

EEPROM UVR2 配置的字节布局、地址、CRC 与旧格式解析不变，无迁移。主机回归覆盖 VFO/MR 切换、频道失效、多路守听、卡片视图、接收回滚与提交、芯片采样与音频时序、保存及纯调谐逻辑。实机芯片／音频／功放时序需另行硬件验收。

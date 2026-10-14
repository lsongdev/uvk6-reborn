# 源码结构

主屏幕是 **Radio 应用**，与频率来源无关。A/B/C/D 是 `RadioSlot`；每个 Slot 可在 VFO（直接调频）与 MR（存储频道）之间切换。两种来源都解析为同一个 `RxPlan`（设备＋频率＋参数），只经 `core/receiver` 下发到 `hal/rx-device`。

## 分层

```text
apps/radio*.c        用户意图：Slot、VFO/MR、调谐事务、Seek/FC/Watch/PTT、菜单与卡片
core/tuning          纯逻辑：解析 Slot 来源、精确规划、VFO 连续步进
core/radio-model     频段表、模式、参数 → RxPlan（自动选择 BK4819 / SI4732 / BK1080）
core/receiver        硬件当前所听：唯一的 RxPlan、RSSI 采样、静噪与音频开关
hal/rx-device        唯一知道芯片差异的地方：调谐、采样、静噪/亚音、音频门、Seek、FC
drivers/             BK4819、SI4732、BK1080、ST7565 与 DP32G030 外设
```

上层只看频段、频率和模式；芯片选择由 `RADIO_MODEL_Plan` 决定，芯片寄存器、单位（10 Hz）、带宽索引和 SNR/RSSI 口径只出现在 `hal/rx-device` 与驱动中。

## 目录

| 路径 | 职责 |
| --- | --- |
| `apps/radio.c` | Radio 控制器：Slot 选择、VFO/MR 解析、调谐事务（`request`/`apply`/`tuneExact`）、按键分派 |
| `apps/radio-seek.c` | Seek：临时调谐候选，只在确认找到时提交 |
| `apps/radio-capture.c` | FC 频率捕获与临时 NFM 试听 |
| `apps/radio-watch.c` | 多路守听及暂停后恢复主接收 |
| `apps/radio-tx.c` | PTT、忙碌禁发、TX 安全时限、各 Slot TX 设置 |
| `apps/radio-menu.c` | Slot 菜单（RX 参数、Watch、TX、频道命名/保存） |
| `apps/radio-view.c` | 卡片视图数据、标题与调谐确认页 |
| `apps/radio-internal.h` | 上述模块间的私有共享状态，不是公共接口 |
| `apps/settings.c`、`apps/bands.c` | 全局设置与频段预设编辑 |
| `apps/channel-manager.*` | MR 频道命名、保存与覆盖确认 |
| `core/radio-state.*` | `RadioSlot`：VFO 频段记忆、来源、选中项 |
| `core/watch.*`、`core/seek.*`、`core/capture.*` | 多路守听、Seek、FC 的纯状态机 |
| `core/rx-runtime.*`、`core/tx-runtime.*` | 已提交接收方案的回滚，半双工 TX 生命周期 |
| `core/config-store.*`、`core/channel-store.*` | UVR2 配置双副本与用户频道存储 |
| `ui/components/radio-card.*` | Slot 卡片绘制；无状态、不滚动 |
| `tests/` | 主机回归；`sh tests/run.sh` |

## 约定

- VFO/MR 是 **来源**（`RadioSource`），NFM/WFM/AM/LSB/USB 是 **解调模式**（`RxMode`）。
- `selectedSlot` 是用户操作对象，`receivingSlot` 是当前物理接收归属，`gReceiver.plan` 是硬件此刻所调谐的方案，`runtime.current` 是用户已提交、失败时恢复的方案。四者不能混用。
- MR 频道只解析为临时有效状态，不覆盖 Slot 的 VFO 记忆；切回 VFO 恢复原状态。
- 纯逻辑层（`core/tuning`、`radio-model`、`watch`、`seek`、`capture`）不访问 EEPROM、显示、时钟或 HAL。
- Cortex-M0 无硬件除法：避免有符号 `/`、`%`，否则会链接 468 B 的 `__divsi3`。
- 不使用 printf：文本用 `core/text` 拼接（有界、自动截断），`Print*` 和 `STATUSLINE_SetText` 只接收现成字符串。

`main.c` 负责硬件初始化，`apps/system.c` 的主循环调用 `RADIO_APP_TxSafetyUpdate()` 维护最高优先级的 PTT 安全释放，Radio 应用退到 Settings 也不会停止。默认构建开放 TX 和 FC：`make ENABLE_TX=0` 为仅接收，`make ENABLE_FC=0` 去掉 FC 频率捕获。

详细边界见 [Radio 架构](../docs/radio-architecture.md)。回归运行 `sh tests/run.sh`，ARM 构建运行 `make -B -j4 all`。

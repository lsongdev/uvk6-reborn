# 源码结构

A/B/C/D 是 `RadioSlot`；每个 Slot 可在 VFO（直接调频）与 MR（存储频道）之间切换。两种来源都解析为同一个 `RxPlan`（设备＋频率＋参数），只经 `core/receiver` 下发到 `hal/rx-device`。

射频由常驻的 **Radio 控制器**（`radio/`）运行，不属于任何页面；页面（`screens/`）是一个栈，主屏幕只是栈底的一页。进入设置、频道列表时，守听、RDS 和 TX 安全检查照常运行。

## 分层

```text
system.c             主循环：按键分发、RADIO_Update、页面栈更新与绘制
screens/             页面栈：主屏幕、Slot 菜单、保存频道、设置、频段、频道、输入、状态栏
radio/               Radio 控制器：Slot、VFO/MR、调谐事务、Seek/FC/Watch/PTT、RDS
ui/                  纯显示工具：绘图、字体、菜单、数字输入、卡片/频率/信号/电池绘制
core/                纯逻辑与存储：调谐、频段模型、接收器、守听/Seek/FC 状态机、配置
hal/                 芯片差异：调谐、采样、静噪/亚音、音频门、Seek、FC、按键、显示
drivers/             BK4819、SI4732、BK1080、ST7565 与 DP32G030 外设
```

依赖只向下：

- `screens/` 可以使用 `radio/` 和 `ui/`。
- `radio/` 不包含 `ui/` 或 `screens/`；它只通过 `radio/radio.h` 暴露意图（调谐、切换、Seek、PTT……）和只读状态（`RADIO_SlotStatus`）。
- `ui/` 不知道射频和页面，只绘制传入的数据。
- `core/`、`hal/`、`drivers/` 不包含以上任何一层。

上层只看频段、频率和模式；芯片选择由 `RADIO_MODEL_Plan` 决定，芯片寄存器、单位（10 Hz）、带宽索引和 SNR/RSSI 口径只出现在 `hal/rx-device` 与驱动中。

## 目录

| 路径 | 职责 |
| --- | --- |
| `system.c` | 主循环、全局按键（键盘锁、长按 MENU 进设置、EXIT 返回）、TX 安全检查 |
| `radio/radio.h` | 控制器公共接口：页面只经由它操作射频 |
| `radio/radio.c` | Slot 选择、VFO/MR 解析、调谐事务（`request`/`apply`）、RDS、状态快照 |
| `radio/seek.c` | Seek：临时调谐候选，只在确认找到时提交 |
| `radio/capture.c` | FC 频率捕获与临时 NFM 试听 |
| `radio/watch.c` | 多路守听及暂停后恢复主接收 |
| `radio/tx.c` | PTT、忙碌禁发、TX 安全时限、各 Slot TX 设置 |
| `radio/radio-internal.h` | 上述模块间的私有共享状态，不是公共接口 |
| `screens/screen.*` | 页面栈：`SCREEN_Reset`/`Push`/`Pop`，最多 6 层，栈底不可弹出 |
| `screens/home.*` | 主屏幕：卡片、标题、键盘频率输入、F 组合键、调谐确认 |
| `screens/slot-menu.*` | Slot 菜单（RX 参数、Watch、TX、恢复默认、另存频道） |
| `screens/save-channel.*` | MR 频道命名、保存与覆盖确认 |
| `screens/settings.*`、`bands.*`、`channels.*` | 全局设置、频段预设编辑、频道列表（改名/删除） |
| `screens/text-input.*`、`reset.*` | T9 文本输入、首次初始化 |
| `screens/statusline.*` | 状态栏：标题、F、EEPROM 写入、SI、锁、电池 |
| `ui/radio-card.*` | Slot 卡片绘制；无状态、不滚动 |
| `ui/menu.*`、`number-input.*`、`numnav.*` | 五行菜单、数字输入与数字跳转 |
| `core/radio-state.*` | `RadioSlot`：VFO 频段记忆、来源、选中项 |
| `core/watch.*`、`core/seek.*`、`core/capture.*` | 多路守听、Seek、FC 的纯状态机 |
| `core/rx-runtime.*`、`core/tx-runtime.*` | 已提交接收方案的回滚，半双工 TX 生命周期 |
| `core/config-store.*`、`core/channel-store.*` | UVR2 配置双副本与用户频道存储 |
| `tests/` | 主机回归；`sh tests/run.sh` |

## 约定

- VFO/MR 是 **来源**（`RadioSource`），NFM/WFM/AM/LSB/USB 是 **解调模式**（`RxMode`）。
- `selectedSlot` 是用户操作对象，`receivingSlot` 是当前物理接收归属，`gReceiver.plan` 是硬件此刻所调谐的方案，`runtime.current` 是用户已提交、失败时恢复的方案。四者不能混用。
- MR 频道只解析为临时有效状态，不覆盖 Slot 的 VFO 记忆；切回 VFO 恢复原状态。
- 页面私有的交互状态（输入草稿、F 前缀、菜单光标）留在页面里，控制器不知道它们。
- 纯逻辑层（`core/tuning`、`radio-model`、`watch`、`seek`、`capture`）不访问 EEPROM、显示、时钟或 HAL。
- Cortex-M0 无硬件除法：避免有符号 `/`、`%`，否则会链接 468 B 的 `__divsi3`。
- 不使用 printf：文本用 `core/text` 拼接（有界、自动截断），`Print*` 和 `STATUSLINE_SetText` 只接收现成字符串。

`main.c` 负责硬件初始化，`system.c` 的主循环每轮先调用 `RADIO_TxSafetyUpdate()` 维护最高优先级的 PTT 安全释放，再调用 `RADIO_Update()`，与当前页面无关。默认构建开放 TX 和 FC：`make ENABLE_TX=0` 为仅接收，`make ENABLE_FC=0` 去掉 FC 频率捕获。

详细边界见 [Radio 架构](../docs/radio-architecture.md)。回归运行 `sh tests/run.sh`，ARM 构建运行 `make -B -j8`。

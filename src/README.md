# 源码结构

主屏幕是 **Radio 应用**，与频率来源无关。A/B/C/D 是 `RadioSlot`；每个 Slot 可在 VFO（直接调频）与 MR（存储频道）之间切换。两种来源都转成 `RxPlan`，接收器仍由 `rx-runtime` 和 HAL 驱动。

| 目录 | 职责 |
| --- | --- |
| `apps/radio.c` / `apps/radio.h` | Radio 主屏幕生命周期、按键、菜单、临时 Seek/FC/Watch、PTT 协调及显示状态组装 |
| `apps/settings.c` | 全局设置应用；与 Radio 均在 `appsAvailableToRun` 中启用 |
| `apps/channel-manager.*` | MR 频道命名、保存与覆盖确认的交互界面 |
| `core/radio-state.*` | `RadioSlot` 和各槽 VFO 记忆、来源、选中项及 Watch 配置 |
| `core/tuning.*` | 纯逻辑：解析槽的 VFO/MR 来源、保存频率的精确接收规划、VFO 步进 |
| `core/radio-model.*` | 频率、解调模式和设备能力到 `RxPlan` 的规划 |
| `core/rx-runtime.*` / `core/tx-runtime.*` | 接收失败回滚及半双工 TX 生命周期 |
| `ui/components/radio-card.*` | A/B/C/D 通用卡片绘制、名称/RDS/频率滚动，不访问频道存储 |
| `hal/` | 整机设备资源及芯片驱动适配 |
| `drivers/` | 具体射频、屏幕芯片及 MCU 访问 |
| `tests/` | 主机回归；Radio、调谐、存储及屏幕组件测试分开维护 |

VFO/MR 是 **来源（`RadioSource`）**，NFM/WFM/AM/LSB/USB 是 **解调模式（`RxMode`）**。主屏幕同时呈现用户选中槽与物理接收槽：选中槽负责输入、菜单及调谐意图；接收槽表示当前 RX/Watch 的硬件归属。两者不能混用。

MR 模式的频道数据仅解析为临时的有效状态，不覆盖槽内 VFO 最后频率、各频段记忆；切回 VFO 应恢复原状态。频道选择、Slot 切换、接收失败回滚、频率捕获与 Seek 的保存时机在 Radio 应用中协调，纯 `tuning` 层不访问 EEPROM、显示、时钟或 HAL。EEPROM UVR2 格式 8 的字段顺序没有改变。

`main.c` 负责硬件初始化，`apps/system.c` 维护整个系统的最高优先级 PTT 安全检查和按键调度。生产 TX 默认禁用，不能因为 Radio 应用退到 Settings 而停止 PA 安全释放。

详细边界和迁移说明见 [Radio 架构](../docs/radio-architecture.md)。完整回归运行 `sh tests/run.sh`；ARM 构建运行 `make -B -j4 all`。

双守听由 `core/watch.*` 按芯片决定：同芯片时分轮询，不同芯片并行接收。`hal/rx-device.*` 分开配置接收芯片与选择音频出口；`core/radio.*` 的前台镜像用于当前播放／测量，不表示后台芯片已关闭。并行 SI WFM 的 RDS 独立归属其接收目标。

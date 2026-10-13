# Radio 主屏幕架构

2026-10-13。当前实现已迁移至 `src/apps/radio.c`。本文件描述已落实的边界与仍在 Radio 应用内的协调逻辑。

## 概念

- **Radio App**：主屏幕、快捷键、参数菜单、提示、生命周期。与接收频率的来源无关。
- **RadioSlot（A/B/C/D）**：独立工作位置，最多四项、每页显示两项。含 VFO 频段记忆、当前来源、MR 频道编号、TX 配置及 Watch 资格。
- **RadioSource**：`RADIO_SOURCE_VFO`（直接调谐）或 `RADIO_SOURCE_CHANNEL`（MR 频道）。
- **RxMode**：NFM、WFM、AM、LSB、USB，是明确的解调模式，不是 VFO/MR 来源。
- **RxPlan / RxRuntime**：设备选择、有效调谐、失败恢复与归属，不依赖屏幕名称或频道来源。

## 数据流

```text
RadioSlot.source
    ├── VFO: stored.current + stored.bands
    └── MR: CHANNEL_STORE_Load(channel_number)
                  ↓
         TUNING_Resolve (effective slot, 临时)
                  ↓
       TUNING_ExactPlan → RxPlan
                  ↓
       RX_RuntimeTune / HAL → device
                  ↓
       Radio App 状态、用户提示和持久化
                  ↓
       UI_RadioCardView → radio-card
```

`TUNING_Resolve` 仅构造有效视图；无 IO、副作用和隐藏状态。MR 频道名称、频率、RX/TX 配置来自频道数据，但绝不直接修改持久化的 VFO 频段记忆。`TUNING_StepVfo` 只接受 VFO 来源；MR ▲/▼ 则由 Radio 应用调度频道编号。主屏幕使用同一个 `radio-card` 绘制两者。

**调谐是事务**：先校验新计划，再通过 `RX_RuntimeTune` 应用；应用失败不提交新的 Slot 选中状态、来源或 EEPROM 保存。直接输入仍允许硬件量化确认；已保存的频道、Watch 候选与 Slot 切换要求目标频率能精确表示。Seek/FC/Watch 暂时接管硬件，但不是第三种 RadioSource。

## UI 与安全边界

`selectedSlot` 表示用户操作对象，`receivingSlot` 表示当前物理接收归属，`inputSlot` 防止频率草稿串到其他槽。主屏幕负责生成卡片数据，`ui/components/radio-card.c` 只绘制并维护动画状态。全局 Settings 与 Radio 同为启用的独立应用；频道编辑、频段编辑和首次初始化作为内部工作流仍可调用。

PTT 物理松开、电池电压和 TOT 的系统级安全处理仍由 `apps/system.c` 的主循环调用 `RADIO_APP_TxSafetyUpdate()`，不能依赖画面是否可见。生产构建仍默认关闭硬件 TX。

## 兼容范围与验证

数据结构和 C 符号已经重命名，但 EEPROM UVR2 格式 8 的序列化字节顺序、存储地址、CRC 以及旧格式解析保持不变；无额外迁移层。主机回归测试覆盖 VFO/MR 切换、频道无效、双守听、卡片视图、RX 回滚、保存及独立的纯调谐逻辑。

本次先调整依赖方向、去除 VFO 命名歧义，不把共享 RF 运行时强行抽象成框架；Seek、FC、Watch 的协调逻辑仍保留在 Radio 应用。实机芯片/音频/功放时序还需硬件验收。

# 全局设置菜单重规划

日期：2026-10-13。首批菜单与配置来源统一已实现；本文保留规划及取舍，当前操作以用户手册为准。

当前已实现：七组菜单、静态偏好描述表、单一 preferences 运行态、统一延迟保存与错误状态、校准与 Reset prefs 的保存后提交、退出设置时重新应用接收工程参数、About／TX status 只读信息。UVR2 格式仍为 8，自动锁键与按键音保留为后续工作。

## 范围与原则

全局设置负责整台设备的显示、操作习惯、接收策略和发射保护。模式、步进、带宽、静噪等级、收发亚音、发射频率、功率、单项 Watch 留在 VFO／频道参数中；频段默认参数继续由 Bands 管理。

固定目标为官方绿色 UV-K6 512 大内存版本：EEPROM 512 KiB，应用 Flash 60 KiB、RAM 16 KiB。EEPROM 容量和芯片类型由平台探测并显示，不允许通过普通菜单修改。新增菜单优先使用静态表和现有五行列表，不增加堆分配、中文字体或独立菜单框架。

每个可编辑项必须具备：合法值、确定的生效时机、运行时消费路径、持久化及失败反馈。仅有旧字段或驱动函数的功能不能进入正式菜单。

## 建议菜单

根菜单依次为 Display（显示）、Receive（接收）、Transmit（发射）、Lock PTT too、Battery（电池）、Band presets（频段预设）、System（系统）、About（关于）。单项设置直接显示；Display 与 Receive 内的背光、静噪和接收硬件参数不再增加一层子菜单，超过五项时滚动。About 固定为根菜单最后一项，System 保留 TX status、Save status 和 Reset prefs。未来按键设置增加后再评估分组。屏幕沿用英文，中文名称用于设计与手册；实际排版核对名称与右侧值的宽度，避免挤压。

| 路径／屏幕名称 | 设置内容与范围 | 默认策略／生效时机 | 实施范围 |
| --- | --- | --- | --- |
| Display → Contrast | 沿用合法硬件范围 0～15，界面显示 -8～+7 | 默认硬件值 8；即时预览 | 首批 |
| Display → Light time | Off、5/10/20/60/120 s、Always | 默认 20 s；即时 | 首批 |
| Display → Brightness | 工作亮度，沿用 0～15 档 | 默认 8；即时预览，具体可见亮度实机验收 | 首批 |
| Display → Idle light | 待机亮度，沿用 0～15 档；限制 ≤ 工作亮度 | 默认 0；即时更新待机状态 | 首批 |
| Display → Light on RX | Off／Timed／Receiving | 默认 Timed；解释为信号触发续时／随接收关闭，而非解调模式 | 首批 |
| Receive → VFO count | 2／3／4 | 默认 2；减少数量保留隐藏项，关闭隐藏 Watch，必要时选择 A | 首批 |
| Receive → Dual watch | Off／On | 默认 Off；退出菜单后恢复调度，仍最多两项参与 | 首批 |
| Receive → SQL open delay | BK4819 静噪开启时序，硬件代码 0～7 | 默认 1；退出菜单重新应用当前接收方案 | 首批 |
| Receive → SQL close delay | BK4819 静噪关闭时序，硬件代码 0～3 | 默认 1；同上 | 首批 |
| Receive → SI4732 idle | Mute／Power off，离开 SI 接收路径时的策略 | 默认 Mute；下一次离开 SI 路径生效 | 首批；无 SI 时不可编辑 |
| Receive → Filter split | 240／280 MHz，BK 接收滤波切换边界 | 默认 240 MHz；退出菜单重建当前接收方案 | 首批；工程项 |
| Transmit → Time limit | 30／60／120／180／300／600 s | 默认 120 s；下次 PTT 使用 | 首批 |
| Transmit → Block if busy | Off／On，忙信道发射保护 | 默认 On；下次 PTT 使用 | 首批 |
| System → TX status | 只读硬件发射许可状态及阻止原因 | 当前实际 PA 输出仍禁用 | 首批，只读 |
| Lock PTT too | 键盘锁定时是否同时拦截 PTT | 默认 Off；沿用现有 ptt_lock 语义 | 首批 |
| Keys → Auto lock | Off／30／60／120 s，无操作后锁键 | 建议默认 Off；仅在主界面且非 PTT、输入、菜单时计时 | 后续新增 |
| Keys → Key beep | Off／On，按键反馈 | 默认 Off；必须先接入实际音频路径再开放 | 后续新增 |
| Battery → Show charge | Icon／Percent／Voltage | 默认 Percent；即时刷新 | 首批 |
| Battery → Charge curve | 1600／2200／3500 对应现有电压百分比曲线 | 首批沿用已保存值／当前默认；它不是实际容量测量 | 首批 |
| Battery → Cal voltage | 输入实测电压 5.00～8.60 V；显示当前电压 | 沿用当前校准；校准系数校验成功才提交 | 首批 |
| Band presets | 打开现有频段预设编辑器 | 保留草稿、Use current、Defaults、Save 语义 | 首批，功能入口 |
| About | 固件版本、接收芯片／补丁能力、EEPROM 容量、配置格式 | 自动探测／只读，可分页 | 首批 |
| System → Save status | Saved／Pending／Failed／Read only；失败可按 MENU 重试 | 区分内存生效与 EEPROM 写入成功 | 首批 |
| System → Reset prefs | 恢复普通全局偏好 | 显示范围并确认；成功写入后提交和重应用 | 首批，需新增专用动作 |

命名含义：Duration＝背光持续时间；Brightness＝点亮亮度；Idle level＝熄灯后的亮度；On signal＝收到有效信号时的背光行为（Timed 按持续时间熄灭，Receiving 随接收音频关闭而熄灭）。Open delay／Close delay＝静噪打开／关闭延迟；SI4732 idle＝离开该芯片后的待机方式；Filter split＝接收滤波器切换频率。Time limit＝单次发射时限；Block if busy＝有信号时禁止发射；Lock PTT too＝锁键时同时锁定发射键。Show charge＝电量显示方式；Charge curve＝电量估算曲线；Cal voltage＝校准电压。Reset prefs＝恢复普通全局偏好，不重置整台设备。Band presets 明确这是频段默认参数编辑入口。

根菜单和上述中英文对应关系用于实施；VFO、PTT、SI4732 保留通用功能／器件名称，避免把 VFO 数量误写成可接收的频段数量。双守听是轮流检查两个目标，不暗示两路同时接收。自动锁键与按键提示音仍为后续功能。

电池曲线未来可以改为依据已验证电池包的更明确命名；本轮不猜测当前安装电池容量，也不把容量数字当作耗电积分。

静噪时序当前代码用 `v * 5 ms` 显示，但增减范围处仍有 datasheet TODO。首批必须限制开启／关闭各自的硬件代码范围；在寄存器时序换算核实前显示 `Level 0…7/3`，不承诺精确毫秒值。后续核实后通过显式映射表显示单位。

`Lock PTT too` 保持现有含义：只有键盘锁定时才影响 PTT。长按 # 的现有锁键快捷操作继续保留，不增加一个会立即锁死菜单输入的“当前锁键”项目。若未来需要整机发射总开关，应新增独立字段，并由 TX 授权链执行，不复用 ptt_lock。

## 本轮不开放的旧项目

- 扫描模式、扫描列表、命中／消失停留时间：Makefile 排除了旧扫描核心，先设计可达的扫描运行时再增加 Scan 子菜单。
- VOX、省电周期、Roger、尾音消除、DTMF 解码：必须确认当前运行时完整实现，不能因为旧 Settings 留有字段就显示。
- Mic、Deviation：现有字段与旧 BK 发射驱动有关；真实发射链完成并验证后再设计 Audio／TX Advanced，不把寄存器值包装成已经可用的麦克风设置。
- EEPROM 类型／容量、任意频率校正、硬件发射解锁：不作为普通用户偏好。校准独立管理，发射许可由能力及已验证标定决定。
- 启动 App、旧 Multiwatch、活动 VFO 索引：单一 Radio 入口和新的 radio 状态已经承担这些职责。当前频道显示需求单独结合频道模式设计处理。

## 交互与生效

沿用长按 MENU 打开 Settings；▲／▼移动，* 增加，# 减少，MENU 进入子菜单或执行动作，EXIT 返回。代码中的减少键标识为 KEY_F；文档按实体 # 标注。普通标量不另设编辑模式，也不把 MENU 同时解释为“确认保存”。

显示／电池项修改后立即预览；EXIT 不撤销普通偏好。接收相关工程项在菜单内更新合法配置，退出 Settings 时经接收运行时重新应用；失败保留上一份可用接收配置并反馈原因。实现需复用现有接收方案的恢复逻辑，禁止从菜单直接重放旧 RADIO 初始化流程。守听在菜单期间继续暂停，退出后按现有等待策略恢复。

SI4732 idle 是下一次路径切换策略；TX 保护项在下一次 PTT 获取，不在发射过程中改变当前保护快照。不可用功能显示短原因且不修改，例如 `No SI4732`。不需要把实时音量旋钮、当前电压、RSSI 变成可编辑偏好。

校准与 Reset prefs 使用小范围草稿：MENU 提交，EXIT 取消。校准必须验证最终系数与配置编码范围一致；当前有效系数范围为 1900～2154，输入电压合法并不代表计算所得系数合法。Reset prefs 只恢复普通全局偏好，保留电压校准、电池曲线、RF／静噪工程参数、VFO／频道／频段预设、Watch 状态及硬件资料。显示、按键、TX 保护等恢复列表应在确认页可查阅。它不能复用现有初始化 APP_RESET，因为后者用于缺失 UVR2 配置时初始化整份配置。

## 配置与代码结构

重构前 `gSettings` 是旧位域状态，`gDeviceConfig.preferences` 是 UVR2 明确字段；当时 `SETTINGS_Load()` 从 preferences 导入，`DEVICE_SaveNow()` 又通过 exportPreferences 反向覆盖。与此同时 VFO 数量、守听和 TX 保护直接修改原生配置。重构应统一到 `gDeviceConfig`，避免新旧副本覆盖。

实现职责如下：

| 模块 | 职责 |
| --- | --- |
| `apps/settings.c` | 静态菜单分组、输入动作、简短值文本和能力提示；不直接操作射频寄存器 |
| `core/settings.h/.c` | 可编辑偏好的 ID、范围及选项，`SETTINGS_SetValue(id, value)` 校验并返回是否接受；同值不写入，接收硬件生效由设置应用退出动作处理 |
| `core/device-config.c` | 唯一的延迟保存计时、dirty／失败状态和重试；移除 exportPreferences 与重复的旧保存计时 |
| `core/config-store.c` | 默认值、字段校验、版本解码、显式字节编码和双槽提交；不调用硬件 |
| `core/radio-state.c`／接收运行时 | VFO 数量与 Watch 约束、接收方案应用和失败恢复 |
| `hal/backlight.c`／电池／TX 模块 | 接收已经校验的具体配置；背光更新不必每次重新初始化 PWM |

偏好描述表只保存必要元数据，使用 const 放入 Flash；普通数值与枚举共用格式化／步进逻辑，校准和恢复默认值保留专用动作。不要为每个布尔项复制 getter/setter，也不要建立可动态注册的设置系统。

`radio.vfo_count` 和 `watch_enabled` 仍属于 radio 状态；全局菜单通过状态 API 修改，不为了表驱动强行复制进 preferences。EEPROM 探测结果、芯片能力、保存状态是独立只读状态，不能混入可保存偏好。旧 Settings 中仍有消费者的字段逐个审计并同步迁移；删除无消费者的 ID，而不是增加旧调用方式的兼容包装。

## 存储与提交

第一步只重排已有设置、统一内存来源和添加只读页，不改变 UVR2 格式 8 的字节布局。保留旧格式中的暂不开放字段，使已有配置正常往返；单一内存来源不等于立即删除磁盘字段。

新增 Auto lock 等字段再升级存储版本，显式解码格式 8 并为新字段赋默认值。沿用 `0x0800–0x0FFF` 的双 1024 B 槽；当前文档记录为 818 B，新增字段须以实际编码长度核对余量。不要改变频道双副本区、原厂校准区和尾部 SSB 补丁。

统一为最后一次成功变更后 1500 ms 合并写入，仅值实际改变时设 dirty。顶层 EXIT 可尝试提前提交待保存项，失败仍允许返回接收并保留 Pending／错误状态，不宣称 Saved。写入过程仍使用备用槽、校验和回读；失败不能清 dirty。持续失败时限制重试频率并提供手动重试，避免每次刷新都触发 EEPROM 写入。保存状态只表示存储结果，不声称硬件已应答。

普通偏好在写入前已生效，突然断电可能丢失最近尚未提交的修改；校准和 Defaults 应先验证／保存候选配置，成功后替换运行态。已有配置不可写时显示失败并保留原值。

## 实施顺序与验收

1. 审计可达消费者，迁移至唯一配置源；统一默认值、校验与保存状态。完成后确保无 exportPreferences 回覆盖和双计时器。
2. 重排首批菜单，修正 SQL 范围、校准系数检查、低亮度联动；添加 About 和保存反馈；Beep 在接入音频前不开放。
3. 接入退出菜单后的接收方案重应用、背光即时预览、专用 Reset prefs 草稿与提交；同步用户手册。
4. 在上述行为稳定后实现 Auto lock 和 Beep；新增字段时升级配置格式及迁移测试。扫描、VOX 和真实 TX 音频另立功能计划。

主机回归覆盖：每项上下界／循环、非法值不产生副作用；设置保存与重启往返；格式 8 迁移；未变更不写 EEPROM；写入／回读失败不清 dirty；恢复偏好不改变校准及 radio 状态；无芯片不可修改；SQL 与滤波调整的接收恢复；减少 VFO 数量的选择／Watch 约束；菜单进退及长按按键重复。

实机验收覆盖：背光 Off／Always／工作与待机亮度、锁键与 PTT release、接收中修改 SQL 后的音频、跨 SI/BK 的待机策略、双守听退出菜单恢复、断电后最后一次已提交配置、失败提示可读性。新增音频与自动锁键另外验证不会干扰亚音检测、PTT 和守听。构建检查 Flash ≤60 KiB、静态 RAM 加栈预算 ≤16 KiB；EEPROM 扩容不抵消这两项限制。

## 源码依据

- 现有菜单：[apps/settings.c](../src/apps/settings.c)、五行渲染及按键：[ui/menu.c](../src/ui/menu.c)。
- 旧状态与范围：[core/settings.c](../src/core/settings.c)、原生字段及版本：[config-store.h](../src/core/config-store.h)、默认与编码校验：[config-store.c](../src/core/config-store.c)。
- 导入／导出与延迟保存：[device-config.c](../src/core/device-config.c)；静噪缓存和 SI 离开策略：[rx-device.c](../src/hal/rx-device.c)。
- 键盘锁语义：[system.c](../src/apps/system.c)；当前功能编译范围：[Makefile](../Makefile)。

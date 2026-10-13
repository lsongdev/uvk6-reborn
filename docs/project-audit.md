# 项目审计与 Flash 优化建议

2026-10-13；基于 847dd04 加未提交工作区（菜单 RX Watch 前移、允许 1 个保存项）。工具链 ARM GNU 15.3.rel1，release `-Os`／LTO。问题 1–4 已在同一工作区修复，见下文“修复记录”；Flash 基线及历史测量见 [Flash 占用审计](flash-budget.md)。

## 当前状态

- 审计时 Flash **59,700 / 61,440 B**，余 1,740 B；修复后 **59,668 B**，余 **1,772 B**；链接 RAM 8,088 / 16,384 B 不变。
- 审计时 `sh tests/run.sh` 失败（问题 1）；修复后全部通过，ARM 构建零警告。
- 未刷机、未实机验证。

## 问题

### 1. `radio-app-regression` 失败

`tests/radio-app-regression.c:573` 断言失败。工作区把 RX Watch 移到菜单第 5 项，TX Enable／Power／Frequency／Tone 依次后移；`src/apps/radio.c` 的 `changeValue`、`menuValue`、`renderMenu` 已同步，但测试仍按旧顺序（570–573、673、685、690、711 行）。

根因是菜单项使用裸数字（`menuIndex == 6/7/8/9`、`renderMenu` 中 `i == 8`）。建议完整枚举 `MENU_RX_MODE … MENU_TX_TONE`，代码与测试均使用枚举名；前 5 项须保持与 `RadioParameterField` 一致（`changeValue` 直接强转）。不影响 Flash。

### 2. `bk4819.c:310` 变量 `reg` 可能未初始化

`switch` 未覆盖 0–3、-1 以外的输入时读取未定义寄存器地址。补 `default:` 分支。

### 3. 编译警告被屏蔽

- `-Wno-incompatible-pointer-types`：GCC 15 默认将其视为错误。当前仅掩盖 `bk1080.c:91` 向 `I2C_WriteBuffer` 传入 `uint16_t *`；功能正确，改为显式 `(const uint8_t *)` 后可移除该选项。
- `-Wno-unused-*` 掩盖的死代码：`battery.c:77 mulipl`、`scheduler.c:5 elapsedMilliseconds`、`bk4819.c:82 AGC_FAST`、`BK4819_SetAGC` 的 `useDefault` 参数。

### 4. `watchUpdate` 重复分支

`radio.c` 中 `!watch_enabled` 与 `slot_count < 2` 两段提前返回完全相同，可合并为一个条件。

## 修复记录

1. `radio.c` 菜单改为完整枚举 `MENU_RX_MODE … MENU_CHANNEL_SAVE`，RX 项直接取 `RADIO_FIELD_*` 的值；`changeValue`、`menuValue`、`renderMenu` 与回归测试不再使用裸数字。
2. `BK4819_GetAttenuation` 增加 `default:`，AGC 索引 -1～-4 均读取 `REG_14`。
3. Makefile 移除 `-Wno-incompatible-pointer-types`、`-Wno-unused-function`、`-Wno-unused-variable`。`bk1080.c` 显式转换为 `const uint8_t *`；删除 `mulipl`、`elapsedMilliseconds`、`notificationTimeoutAt`、`APPS_exit` 中未用的 `app`、`AGC_FAST`；`BK4819_SetAGC` 去掉从未生效的 `useDefault` 参数（调用方与测试桩同步，AGC 寄存器写入不变）。
4. `watchUpdate` 两段相同的提前返回合并。

## Flash 分布

| 项目 | 大小 | 说明 |
| --- | ---: | --- |
| `apps/radio.o`（非 LTO 统计） | ~11.8 KB | render 3.7K、update 3.1K、key 2.9K、applyPlan 2.3K、watchUpdate 2.1K（LTO 含内联） |
| 字库（4 套＋符号） | ~3.7 KB | 三套 glyph 表各 665 B（95 × 7 B） |
| 字符串池 | 2,681 B | 271 条 |
| printf | ~1.8 KB | 已禁用浮点、long long、指数 |
| libgcc／libc | ~1.05 KB | `_divsi3` 468 B、`_udivsi3` 276 B |

LTO 价值约 4 KB：同参数关闭 LTO 溢出 4,056 B，必须保留。`dig_11` 已被链接器丢弃。

## 优化方案（按收益／风险排序）

| 方案 | 预计节省 | 风险 |
| --- | ---: | --- |
| A. glyph 元数据 7 B → 5 B：2 B 偏移＋3 B 位域（宽 5、高 5、xAdvance 5、xOffset 3、yOffset 5 位，三套字库值域均可容纳） | ~530 B | 低；用 ui-preview PNG 逐字节对比验证 |
| B. 消除所有有符号除法／取模，使 `_divsi3` 不再链接（约 15 处：`RADIO_MODEL_Adjust`、`GetSql`、`BATTERY_UpdateBatteryInfo`、`calibrate`、`BANDS_key`、`nextBand`、`getValue`、`SETTINGS_render`、`RADIO_GetSNR`、`watchUpdate`、`RADIO_APP_key`／`render` 等） | ~470 B | 中；需逐处确认取值不为负 |
| C. 合并重复／近似字符串：`EEPROM UNKNOWN`／`EEPROM unknown`、`%u.%02uV`／`%u.%02u V`、功率三套标签（`Low/Medium/High`、`LOW/MID/HIGH`、`TX Power: …`） | ~100–200 B | 低 |
| D. `buildCardView` 的 `?:` 链、`menuValue`／`changeValue` 分支改表驱动（可与问题 1 的枚举一起做） | ~200–400 B（未实测） | 中 |
| E. `-fno-jump-tables` | 44 B（实测） | 低 |
| F. 移除 `-fno-delete-null-pointer-checks` | 120 B（实测） | 需先确认；Cortex-M 的 0 地址是向量表，该选项可能有意保留 |

已实测无收益：`-fipa-pta`、`-flto-partition=one`（均 0 B）；收紧 inline 参数（`max-inline-insns-auto=8`、`early-inlining-insns=4`）反增 36 B。与 [Flash 占用审计](flash-budget.md) 中的历史实验一致。

## 建议顺序

1. ~~修复问题 1、2，恢复全部回归通过。~~ 已完成。
2. 实施 A、C（约 600–700 B，低风险）。
3. 视余量需求实施 B、D。

A＋B＋C 合计可将余量从约 1.8 KB 提升至约 2.8–2.9 KB；每项须单独强制构建（`make -B`）实测，按 [Flash 占用审计](flash-budget.md) 的重复检查步骤记录。

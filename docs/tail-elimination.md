# 发射尾音实现与接收尾音规划

2026-10-20。状态：已实现 Slot TX tail，双板构建及主机测试通过；待实机互通与波形确认。RX 尾音静音尚未实现。

## 两种用途

- TX 结束信令：本机松开 PTT 后先静音麦克风，短暂保留载波并发送结束信令，再关 PA，让支持该信令的对方提前静音。
- RX 尾音静音：本机接收时检测对方的结束信令，提前关闭扬声器。单独实现 TX 信令不会消除本机接收时的拖尾；对方按载波静噪接收时也不能保证获益。

## 参考实现

- [DualTachyon BK4819 驱动](https://github.com/DualTachyon/uv-k5-firmware/blob/main/driver/bk4819.c)：`GenTail()` 支持 120°／180°／240° 相移、DCS 134.4 Hz 结束音和 CTCSS 55 Hz 结束音。`radio.c` 的 `RADIO_EnableCxCSS()` 使用 200 ms 持续时间。
- [F4HWN BK4829 驱动，5fedc09](https://github.com/armel/uv-k1-k5v3-firmware-custom/blob/5fedc09e9ddec350823adb9bb2d81df05b52ab8d/App/driver/bk4829.c)：`PlayCTCSSTail()` 可编译选择 180° 相移或 55 Hz；`PlayCDCSSTail()` 发 134.4 Hz。
- [同版 radio.c](https://github.com/armel/uv-k1-k5v3-firmware-custom/blob/5fedc09e9ddec350823adb9bb2d81df05b52ab8d/App/radio.c)：`RADIO_SendCssTail()` 由开关控制，持续 200 ms 后恢复 RX。

这些是驱动与移植参考，不代表任意品牌接收机都兼容。

| TX Tone | 结束方式 | 驱动依据 |
| --- | --- | --- |
| Off | 不增加信令或延迟 | 无亚音时直接恢复 RX |
| CTCSS | 180° 相移 | REG52=0xC28F，保持原 CTCSS 频率与芯片对应增益 |
| CTCSS，未来兼容选项（未实现） | 改为 55 Hz | REG07=0x046F，芯片对应的 CTCSS 模式／增益 |
| DCS N／I | 134.4 Hz 结束音 | REG52=0x828F，REG51：BK4819=0x804A、BK4829=0x8040 |

REG52 的 bit15 必须置位才能使能相移／134.4 Hz 结束音；普通 TX 配置必须恢复 REG52=0x028F，避免下次发射继承尾音模式。不能仅在正常关 PA 后写寄存器，此时已没有结束信令的射频输出。

## 设置

Slot 菜单紧跟 TX Tone 增加 `TX tail: Off / On`，默认 Off，每个 Slot 独立，随频道保存；MR 与其余 TX 参数一致只读。配置与频道使用 TX 标志字节 bit5，记录长度不变，已有记录保持 Off。

On 按亚音类型选择 CTCSS 180°、DCS 134.4 Hz，TX Tone=Off 时不执行。如果目标接收机只识别 55 Hz，再将 CTCSS 方式扩展为可选项；不先开放相位、时长、增益等工程参数。

后续若实现本机 RX 尾音消除，应独立为 `Receive → Tail mute: Off / On`，不能与 TX 开关绑定。对于没有 RX Tone 的载波静噪，不能宣称自动识别任意结束信令。

## 已实现的 TX 时序

- 非阻塞 TX 尾音阶段：正常松键 → 静音麦克风／配置结束信令 → 约 200 ms → PA 先关 → 现有 RX 恢复。通过系统安全轮询检查期限，不阻塞等待；实际载波关断时延仍含主循环调度与总线耗时，需实测。
- 正常松键与紧急停止分开。TOT、电压异常、故障或取消走立即关 PA 的硬停止，不延迟发结束信令。正常释放走 `TX_RuntimeRelease()`，硬停止走 `TX_RuntimeAbort()`。
- 尾音阶段继续检查电压与 TOT；TOT 同样限制尾音，不因为松键延长。重复释放不重启计时；尾音期间重按 PTT 不续发，保留等待松键门禁。尾音结束前后卡片均使用原发射 Slot 归属，尾音期间显示 TX。
- 后续 RX 实现应由现有 CSS／静噪事件泵统一读取结束事件；增加尾音标志、清除过期事件、提前关音频，不能另开读取器抢走事件。
- BK4819／BK4829 的模式与增益差异保留在芯片驱动；状态机和菜单共享。无需改写原厂校准区。

## 验证边界

主机测试已覆盖阶段期限、寄存器、PA 顺序、连续松键、重新按键、计时回绕、TOT／低压／取消中断、正常 TX 清除尾音模式、Slot／频道持久化以及仅接收构建。仍需在假负载上测量结束信令、载波延长和关断时间。互通至少覆盖 K6↔K1、CTCSS／DCS N／I、开关两态与一个其他品牌的接收机。听感、漏字和拖尾均需实机记录。

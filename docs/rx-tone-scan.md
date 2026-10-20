# RX Tone Scan

2026-10-20。独立于 FC 的固定频率亚音识别；已完成代码与主机测试，尚未刷机验证检测速度、弱信号表现及实际互通。

## 操作与状态

- Slot 菜单：RX Tone → RX Tone Scan。只支持 BK NFM；AM、广播和 SSB 不启动扫描。
- 扫描固定当前选中 Slot 的频率，不搜索载频。暂停守听、静音、阻止 PTT 和其他独占 RF 功能。
- Searching 等待对方持续发射；每次硬件采集完成后重新启动，三次同一标准结果才确认。扫描没有总超时，EXIT 随时取消。没有有效结果不能证明没有亚音。
- MENU 只应用当前 VFO 的 RX Tone 并请求一次保存，不改频率、TX Tone、TX Enable、TX tail 或其他 Slot。MR 能识别和查看，但不修改频道；应用提示 VFO only。
- EXIT／页面离开恢复选中 Slot 的原接收计划并延后 2 s 恢复守听；恢复失败保持静音、标记 RX 无效并提示 RX error。未确认不写用户设置。

## 硬件与解析

参考 [DualTachyon BK4819](https://github.com/DualTachyon/uv-k5-firmware/blob/main/driver/bk4819.c)、[F4HWN BK4829 5fedc09](https://github.com/armel/uv-k1-k5v3-firmware-custom/blob/5fedc09e9ddec350823adb9bb2d81df05b52ab8d/App/driver/bk4829.c) 的固定频率扫描配置，以及 [Egzumer DCS 解码](https://github.com/egzumer/uv-k5-firmware-custom/blob/main/dcs.c)。

| 数据 | 来源与处理 |
| --- | --- |
| 扫描模式 | REG51=0x0300，关闭 TX CSS 和自动 CSS 带宽选择；仅启用接收 DSP |
| CTCSS | REG68 bit15=0 时有效，低 13 位 ×4843÷10000 得 Hz×10 |
| DCS | REG69 bit15=0 时有效，69／6A 的低 12 位拼接；再次读 69 验证未撕裂 |
| 标准频点 | CTCSS 最近标准值，误差限 max(1 Hz, 0.5%)；超出不匹配 |
| DCS 码字 | 23 位循环对齐、目录匹配和完整 Golay 码字一致性；不做错误位纠正 |

信号存在由硬件载波静噪判断，扫描临时 SQ 至少为 1。DCS 未完成时继续等待，不按轮询间隔反复重启采集。非标准 CTCSS 值也不重启数字解码器，否则可能因 DCS 的 134.4 Hz 波形测量而永远完不成 DCS 采集。完成读数、损坏 DCS 或失去载波后重新开始，防止把锁存读数当成多个独立样本。

DCS 的 N/I 原始命名不总能从循环码字唯一确定。全部 104 个标准正码的反码都能表示为目录中另一标准正码的循环移位。例如 D023I 与 D047N 的循环码字等效。扫描采用标准正码代表并显示 Equivalent DCS code；原有 RX／TX Tone 菜单仍保留全部 N/I 选项。

## 验证

- 两款真实芯片后端的寄存器模型：固定频率、扫描模式、不启用 PA、有效标志、CTCSS 换算、DCS 拼接、撕裂读数拒绝。
- 50 个 CTCSS 标准值与偏差、误差范围外拒绝；104 个 DCS 正／反码的全部 23 种起始位，共 4,784 个波形组合，以及单比特损坏拒绝。
- 设备测试：载波门限、SQ0 临时调整、不修改原计划、独立重新采集、慢采集等待和非标准 CTCSS 不打断 DCS。
- 控制器与菜单：选中槽归属、回话后恢复选中槽、三次一致／等待／失配重置、PTT 与 RF 排他、VFO 单次提交、TX 与其他槽不变、取消、页面离开、MR 只读、非 NFM 拒绝、启动与恢复故障。
- 全部现有 FC 回归保持原流程；`ENABLE_FC=0` 仍能编译独立 Tone Scan。

以上是主机模型验证。仍需 K6／K1 实机分别确认无亚音、多个 CTCSS、DCS N/I 等效项、弱信号、短时发射和连续重试，不能把寄存器模型当成接收灵敏度或 RF 互通测量。

## K1 刷写记录（2026-10-20）

通过 `/dev/cu.usbserial-21210`、Bootloader `7.02.02` 刷入当前 K1 release，61,196 B 全部分页收到 ACK。镜像 SHA-256：`07830b61bc47879706ce38800584546c89528baffa316e102d81e5522b2b2ac4`。构建为 TX／FC／Spectrum 开启、BK1080（无 SI4732），包含本轮菜单、背光、TX tail 与独立 RX Tone Scan 改动。

镜像、元数据和日志：`backups/uvk1/uvk1-rx-tone-scan-20261020-105848.{bin,json,log}`。本次只刷 MCU 应用分区，不操作外部 Flash 或原厂校准。ACK 不代表 Flash 回读校验；正常重启、识别效果、亮度档位及尾音互通仍待实机确认。

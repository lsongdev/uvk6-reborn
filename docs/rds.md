# RDS 接收与文本解析

2026-10-13：实际接收归属的 VFO 在 **SI4732 接收路径 + WFM 模式** 下轮询 RDS。BK4819 的 NFM/AM、SI4732 的 AM/SSB 和 BK1080 接收路径不读取 RDS。

当前支持 PI 电台识别码、0A/0B 的 8 字符 Programme Service（PS）电台名，以及 2A 的最多 64 字符、2B 的最多 32 字符 RadioText。PS 与 RadioText 放在同一个名称区域，以 ` / ` 拼接，只有一个时直接显示；超宽滚动。调谐后大字号频率保持 1 秒，有文本时正文切换为名称，完整频率移入黑底横条；再次调谐恢复大字号频率。无文本时保持频率。布局见 [VFO 首屏布局](ui-layout.md)。VFO 自定义名称编辑尚未实现。

![当前频率与名称布局](images/vfo-states.png)

上图为当前组件的频率／名称状态示例，不是实机收台截图。运行 `python3 tools/ui-preview.py` 可重建；如需查看模拟 RDS 数据组经过实际解析器的显示，运行 `python3 tools/ui-preview.py --frames plain rds --output /tmp/vfo-rds.png`。

## 数据链路

1. `drivers/si473x/si473x.c` 设置 RDS 接收、FIFO 门限 1 组和最多 1～2 位纠错门限；`SI47XX_ReadRDS` 检查电源、模式、I2C 应答、CTS 和 ERR，将 13 字节响应转换为块、错误等级、同步和 FIFO 状态。失败不修改调用者的输出。
2. `hal/rx-device.c` 将芯片返回的数据适配为与芯片协议无关的 `RdsPacket`。
3. `core/rds-receiver.c` 管理轮询及过期，`core/rds.c` 只处理 RDS 数据组和文本，均不依赖硬件或 UI。
4. `apps/radio.c` 按实际成功应用的接收方案启停 RDS，把 PS/RadioText 送给卡片组件；文本变化时才请求重绘，滚动由组件独立处理。

## 完整性与生命周期

- 每个文本段需要两次一致的有效接收。PS 收齐四段才发布；RadioText 收齐结束符 `0x0D` 前的连续段，或收齐全部 16 段后发布。乱序接收不会提前显示缺段文本。
- A/B 块须无错或已纠正 1～2 位错误。PS 还要求 D 块有效；2A 要求 C/D 块有效，2B 只使用 D 块文本。错误数据不会替换已发布文本。
- PI 改变时清空电台名与文本；RadioText 的 A/B 标志或 2A/2B 格式变化时清空旧文本，保留 PS。动态 PS 保留上次完整名称，等待新名称完整到达。
- 每次实际调谐尝试及停止接收都会清空解析状态，包括失败后恢复旧频率。进入 SI/WFM 后先等待 300 ms，再清空硬件 FIFO，随后每 100 ms 最多读四组；首次读失败就停止本轮。
- FIFO 溢出时重新收集文本。连续 15 秒没有同步且 A/B 块有效的数据组时，文本过期，界面恢复大字号频率。
- RDS 信息不写入 EEPROM。当前使用 ASCII 显示：扩展字符暂以 `?` 代替，普通控制字符转为空格，RadioText 回车作为结束符；不支持完整 RDS 字符集、中文、RT+、时钟或 AF 自动换台。

## 验证与资料

`sh tests/run.sh` 包含解析器、FIFO 轮询、SI 命令与响应、VFO 接收路径切换的回归检查：0A/B、2A/B、重复确认、缺段、末尾段、完整长度、A/B 翻转、PI 变化、块错误、总线失败、过期、定时器回绕及调频失败恢复。主机测试不替代实机信号验证，广播台必须实际发送 RDS 才会出现文本。

芯片命令布局、FIFO 计数、BLE 错误等级及广播数据组示例依据厂商 [AN332：Si47xx Programming Guide](https://www.skyworksinc.com/-/media/Skyworks/SL/documents/public/application-notes/AN332.pdf)，RDS 分组及文本说明参考厂商 [AN243：Using RDS/RBDS with the Si4703](https://www.skyworksinc.com/-/media/Skyworks/SL/documents/public/application-notes/AN243.pdf)。

各 VFO 各自缓存已确认 PS／RadioText，关联对应频率。仅实际使用 SI WFM、守听已经确认信号并驻留（或正常单项接收）的路径轮询 RDS；A/B 切换重置芯片 FIFO 轮询和解析器，不将前一项数据交给后一项。重新调谐清空所属项缓存，活动路径 15 秒无新数据时清空文本；后台行保留其自己的已确认文本，不显示实时接收或信号。

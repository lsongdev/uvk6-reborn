# UVK6 Reborn

面向泉盛 UV-K6 的开源固件，基于 [Hawk5](https://github.com/fagci/hawk5) 演进。

围绕频段、频率和明确解调模式操作，提供多 Slot 守听、频道保存、广播文字、搜台与频率捕获。项目持续开发中，使用前请阅读用户手册，备份设备数据。

## 文档

- [用户手册](docs/user-manual.md)：安装、按键、菜单和日常使用。
- [设计文档](docs/design.md)：功能规则、数据模型和后续规划。
- [开发文档](docs/development.md)：编译、刷机、源码、工具和验证。
- [硬件事实](docs/hardware.md)：当前设备、芯片、按键和存储依据。

## 开始使用

准备好编译环境后：

```sh
make
make flash PORT=/dev/cu.usbserial-1420
```

刷机前关机，按住 PTT 开机。串口路径按实际设备替换；环境依赖、镜像选择和失败恢复见[开发文档](docs/development.md)。

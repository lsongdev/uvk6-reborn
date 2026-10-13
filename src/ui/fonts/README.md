# UI 字库

VFO 正文（主频率、名称、RDS、提示）和频率输入共用 `helvetica12.h`，来源为 Adobe X11 Helvetica Bold `helvB12.bdf` 的可打印 ASCII 子集，95 个字符（32–126）。原生大写字母与数字为 12 px 高，完整 ASCII 笔画含下伸部分占 17 px；不放大、不另选数字字形。字体为比例宽度，数字步进统一 9 px。

`UI_BodyTextWidth` 和 `UI_BodyTextWindow` 使用同一套 glyph 元数据。VFO 的频率和名称共用基线；频率数字右边界 110，名称右边界 128。主频率 `1339.999` 宽 68 px，`438.506` 宽 59 px（含字符步进）。

源 BDF 与许可保存在 `sources/helvB12-ascii.bdf`，生成头部保留完整版权与许可。来源：[U8g2 中的 Adobe X11 原始 BDF](https://github.com/olikraus/u8g2/blob/master/tools/font/bdf/helvB12.bdf)。转换仅选择 ASCII 并重新打包相同点阵，没有修改字形。

重新生成：

```sh
python3 tools/bdf-to-gfx.py src/ui/fonts/sources/helvB12-ascii.bdf src/ui/fonts/helvetica12.h
```

位图 959 B，95 个 GFXglyph 元数据（每个 7 B，合计 665 B，packed 去除尾部填充，不改变字形）；不引入 U8g2 解码器。状态栏／频段／单位的小字继续使用 TomThumb，菜单使用已有中字号字库。

## Adobe X11 字库许可

Copyright 1984-1989, 1994 Adobe Systems Incorporated.
Copyright 1988, 1994 Digital Equipment Corporation.

Adobe is a trademark of Adobe Systems Incorporated which may be registered in certain jurisdictions. Permission to use these trademarks is hereby granted only in association with the images described in this file.

Permission to use, copy, modify, distribute and sell this software and its documentation for any purpose and without fee is hereby granted, provided that the above copyright notices appear in all copies and that both those copyright notices and this permission notice appear in supporting documentation, and that the names of Adobe Systems and Digital Equipment Corporation not be used in advertising or publicity pertaining to distribution of the software without specific, written prior permission. Adobe Systems and Digital Equipment Corporation make no representations about the suitability of this software for any purpose. It is provided "as is" without express or implied warranty.

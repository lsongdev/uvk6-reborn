# UI 字库

固件只有两套文本字体和一套图标：

| 字库 | 尺寸 | 用途 |
| --- | --- | --- |
| `TomThumb.h` | 3×5 | 黑底横条、状态栏、菜单、单位、卡片小字栏 |
| `muMatrix8ptRegular.h` | 5×7 | 卡片名称／频率、文本输入、提示页、菜单标题 |
| `symbols.h` | 8×8 内 | 状态栏图标 |

卡片的大号频率不另设字库：`UI_LargeTextWindow` 把中字放大两倍，每个点向右多画 1 px，笔画因此接近粗体。数字步进 12 px、小数点 4 px，`UI_LargeTextWidth` 恒为 `UI_MediumTextWidth` 的两倍。

2026-10-14 前卡片使用 Adobe X11 Helvetica Bold 12 px（95 个 ASCII 字形，1,434 B），已删除。

## 存储格式

沿用 Adafruit GFX 的位图编码，但每个字形只存 3 字节度量（宽、高、步进、x/y 偏移，位域压缩），不存位图偏移：所有字形的位图首尾相接，每个占 `ceil(宽×高/8)` 字节，绘制时把前面字形的大小累加得到起点。`tests/font-regression.c` 检查每套字库的位图总长恰好等于各字形大小之和，手工改字形时漏改会直接失败。

`tools/bdf-to-gfx.py` 把 BDF 点阵转换为这一格式：

```sh
python3 tools/bdf-to-gfx.py input.bdf output.h --name myFont --line 8
```

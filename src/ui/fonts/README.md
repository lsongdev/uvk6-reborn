# UI 字库

固件只有两套文本字体和一套图标：

| 字库 | 尺寸 | 用途 |
| --- | --- | --- |
| `TomThumb.h` | 3×5 | 黑底横条、状态栏、菜单、单位、卡片小字栏 |
| `muMatrix8ptRegular.h` | 5×7 | 卡片名称／频率、文本输入、提示页、菜单标题 |
| `symbols.h` | 8×8 内 | 状态栏图标 |

卡片的大号频率不另设字库：`UI_LargeTextWindow` 把中字放大两倍，每个点向右多画 1 px，笔画因此接近粗体。数字步进 12 px、小数点 4 px，`UI_LargeTextWidth` 恒为 `UI_MediumTextWidth` 的两倍。

2026-10-14 前卡片使用 Adobe X11 Helvetica Bold 12 px（95 个 ASCII 字形，1,434 B），已删除。

`tools/bdf-to-gfx.py` 仍可把 BDF 点阵转换为本固件的 GFX 格式：

```sh
python3 tools/bdf-to-gfx.py input.bdf output.h
```

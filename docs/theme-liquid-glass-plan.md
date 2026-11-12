# 液态玻璃主题设计方案

> 状态：待评审（未实施）
> 适用范围：QuickShot 桌面端设置窗、工具栏、提示条、历史窗、OCR 结果窗
> 不适用：截图遮罩、贴图窗口、宠物窗口（理由见 §5）

## 1. 目标与边界

目标：新增第三种主题 `liquid`，在现有浅色/深色之外提供接近 iOS 26 / macOS 26 Liquid Glass 的观感：半透明玻璃面、圆角、顶部高光描边、柔和阴影、背景虚化。

明确边界：

| 能力 | 结论 | 说明 |
|---|---|---|
| 玻璃面（半透明底 + 圆角 + 高光描边 + 阴影） | 做 | QSS + 自绘 |
| 背景实时虚化 | 分平台做 | macOS `NSVisualEffectView`、Win11 DWM Acrylic；Linux/Win10 降级 |
| 折射、镜面拉伸、边缘液体形变 | 不做 | 需 Metal/GLSL 着色器，Qt Widgets 无此能力，投入产出比不合适 |

**不做真实折射**是本方案最重要的裁剪决策。Qt Widgets 的窗口内容由 backing store 合成，无法采样下层像素做位移，只有在 QML `ShaderEffect` 或 QRhi 自定义渲染下才可能，而这意味着整站 UI 重写。

## 2. 材质分层

| 层级 | 用途 | 玻璃参数 |
|---|---|---|
| Canvas | 窗口整体底 | 模糊源（P2 才接）；P1 用不透明深蓝灰兜底 `#0E1218` |
| Surface | 设置窗主面板、历史窗卡片、OCR 结果卡 | 圆角 14px、半透明 62%~78%、1px 描边（上白下暗） |
| SubSurface | 分组框、输入区、列表行 | 圆角 10px、白 6%~10% 叠加 |
| Control | 按钮、下拉、开关、标签页 | 圆角 8px、白 10%，悬停白 20%，按下白 8% + 内阴影 |
| Accent | 选中态、主按钮、滑块 | `#4DA6FF`（沿用截屏框蓝，保证品牌一致） |

描边规则（自绘实现）：外圈 1px `rgba(255,255,255,0.28)`，底部内侧 1px `rgba(0,0,0,0.25)`，形成"液体边缘"的立体感；再叠一条 40% 宽度的顶部高光弧（`rgba(255,255,255,0.35)` → 透明渐变）。

阴影：`0 2px 12px rgba(0,0,0,0.18)`；大面板用 `0 9px 28px rgba(0,0,0,0.24)`。禁止对滚动列表每行挂 `QGraphicsDropShadowEffect`（见 §8）。

## 3. 颜色 Token 映射

现有 [StyleColorId](file:///Users/juran/MyFiles/Study/workspace/gitee/quick-shot/src/core/StyleManager.h#L78-L87) 共 20 项，`liquid` 主题为每项给出一套取值（十六进制为 `#AARRGGBB`，沿用现有 `HexArgb` 写法）。

| Token | 浅色（不变） | 深色（不变） | 液态玻璃（新增） |
|---|---|---|---|
| CaptureBorder | #4da6ff | #4da6ff | #4DA6FF |
| RecordBorder | #4caf50 | #4caf50 | #4CAF50 |
| ToolbarBg | #cfcfcf | 现有值 | #CC1B2330 |
| SubToolbarBg | #cfcfcf | 现有值 | #B31A2130 |
| RecordControlBg | #cfcfcf | 现有值 | #CC1B2330 |
| ToolbarBtn | #e0e0e0 | 现有值 | #1AFFFFFF |
| ToolbarText | #000000 | 现有值 | #F2F6FF |
| ToolbarButtonHover | #ffffff | 现有值 | #33FFFFFF |
| ToolbarButtonDisabled | #d1d1d1 | 现有值 | #14FFFFFF |
| ToolbarButtonChecked | #0078d7 | 现有值 | #4DA6FF |
| CloseButtonBg | #cc0000 | 现有值 | #FF453A |
| CloseButtonHover | #ff3333 | 现有值 | #FF6B61 |
| SettingButtonBg | #e0e0e0 | 现有值 | #1AFFFFFF |
| SettingButtonText | #000000 | 现有值 | #F2F6FF |
| TabWidgetBg | #ffffff | 现有值 | #CC121820 |
| TabButtonBg | #e0e0e0 | 现有值 | #14FFFFFF |
| TabButtonText | #000000 | 现有值 | #A8B3C4 |
| TabButtonSelectedBg | #ffffff | 现有值 | #33FFFFFF |
| TabButtonSelectedText | #000000 | 现有值 | #FFFFFF |
| HandleCircle | #2563eb | 现有值 | #4DA6FF |
| HandleClose | #dc2626 | 现有值 | #FF453A |

补充（非颜色表项，跟随主题派生）：正文文字 `#F5F7FA`、次级文字 `#A8B3C4`、分隔线 `rgba(255,255,255,0.12)`、玻璃描边 `rgba(255,255,255,0.28)`。

## 4. 代码改动面

1. **主题三值化**：`s_theme` 由二值改为 `light` / `dark` / `liquid`；`themeMode()` 扩为三值枚举；`themePalette(bool dark)` → `themePalette(ThemeMode)`，新增 liquid 套。旧配置读入未知主题值时按现状回落 `light`，旧版本读到 `liquid` 也会回落浅色，双向兼容。
2. **二值判断审查点**：`themeMode() == ThemeMode::Dark` 的所有调用（标题栏着色、SVG 图标 tint、宠物主题、截图工具提示）、`toolbarIconTint()`、以及 QSS 中按深浅拼装的分支。
3. **QSS 注入**：沿用 `@占位符` 机制，新增 `liquid` 分支的重复样式块（半透明、圆角、描边）；QSS 无法实现阴影与裁切，需抽出统一的自绘面板函数。
4. **自绘面板 helper**：新增一个通用绘制函数（圆角路径 + 描边 + 顶部高光 + 阴影位图），供设置窗、历史窗卡片、OCR 结果卡调用，避免逐控件复制。
5. **设置页**：主题下拉由两项变三项，六语言补 `settings.themeLiquid`。
6. **平台桥接（仅 P2）**：新文件放 `src/platform/`（如 `GlassBackdrop_mac.mm`、`GlassBackdrop_win.cpp`），提供 `enableBackdrop(QWidget*)` / `disableBackdrop()` 的统一接口。

## 5. 应用范围

| 界面 | 是否玻璃化 | 理由 |
|---|---|---|
| 设置窗 | 是（完整玻璃） | 主要展示面 |
| 工具栏 / 子工具栏 / 录屏控制条 | 是（浅玻璃） | 悬浮在桌面，玻璃观感收益最大 |
| 滚动截图提示条 | 是（浅玻璃） | 同上 |
| 历史窗 | 是（卡片玻璃） | 内容以文字/缩略图为主 |
| OCR 结果窗 | 是（卡片玻璃） | 同上 |
| 贴图窗口 | 否 | 窗口内是用户截图，玻璃底会破坏图像还原与颜色准确性 |
| 截图遮罩（SnipScreen） | 否 | 选区像素必须精确；遮罩半透明会干扰用户对选区边界与内容的判断 |
| 宠物窗口 | 否 | 透明窗口 + 自绘宠物，玻璃底会引入不必要的合成层与性能开销 |

结论：玻璃主题是"工具外壳的主题"，不是"内容区主题"。

## 6. 平台能力矩阵与降级

| 平台 | 背景虚化 | 实现 | 观感 |
|---|---|---|---|
| macOS 12+ | 支持 | `NSVisualEffectView`（material `underWindowBackground`、`blendingMode behindWindow`）垫底，窗口 `WA_TranslucentBackground` | 接近系统原版 |
| Windows 11 22621+ | 支持 | `DwmSetWindowAttribute(DWMWA_SYSTEMBACKDROP_TYPE, DWMSBT_TRANSIENTWINDOW)`，Win11 22H2 前用 `SetWindowCompositionAttribute`（非公开） | 接近磨砂 |
| Windows 10 | 部分 | 仅非公开 `SetWindowCompositionAttribute` Acrylic；不稳定，默认关闭 | 磨砂偏灰 |
| Linux | 不支持 | 无系统合成器接口，回退为"纯半透明 + 深色兜底" | 磨砂渐变卡片 |

降级策略：启动时探测能力，不可用则玻璃面自动使用不透明深蓝灰底（P1 的兜底值），保证可读性；在设置页对应主题项下加一行灰色说明文字（如"当前系统不支持背景虚化，已使用纯色玻璃"），不做弹窗。

## 7. 分阶段

### P1：玻璃视觉（不接系统虚化）

- 颜色表新增 liquid 套 + 主题三值化 + 设置页三选项 + 六语言
- QSS 半透明/圆角/描边改造
- 自绘面板 helper（圆角 + 描边 + 高光 + 阴影）
- 验收：三主题切换无残留；玻璃面在浅色/深色/彩色壁纸下文字均可读；贴图、截图遮罩、宠物渲染逐像素无变化（沿用现有 18 样式预览对比脚本）

### P2：系统背景虚化

- macOS `NSVisualEffectView` 桥接（项目已有 `objc_msgSend` 先例）
- Win11 DWM backdrop；Win10/Linux 走降级
- 仅对设置窗、工具栏、提示条启用，历史窗/OCR 窗按性能实测决定
- 验收：macOS 设置窗背后可见真实虚化；关闭能力后自动落回 P1 外观；窗口移动/缩放无闪白

### P3：不做

## 8. 风险

1. **性能**：系统虚化本身开销可控，风险在自绘阴影。滚动列表若逐行画阴影会掉帧，方案要求阴影只画在静态容器上，且用预渲染位图而非 `QGraphicsDropShadowEffect`。
2. **半透明窗口与截图工具叠加**：设置窗开启玻璃时若背后是截图遮罩，虚化结果无意义；不做特殊处理，接受该场景观感下降。
3. **QSS 与自绘混用**：QSS 的 `background-color` 半透明在 `WA_TranslucentBackground` 下依赖窗口属性，部分已有窗口是 Frameless 不透明窗口，需要逐个确认属性，否则半透明会变黑底。
4. **配色回归**：颜色表 20 项被多处直接读取，liquid 套若 alpha 过高会导致文字对比度不足，需按 WCAG AA（正文 4.5:1）逐项核对。
5. **测试覆盖**：14 项单测基本与视觉无关，本主题主要靠人工回归 + 截图对比脚本；需在 P1 交付时附三种壁纸下的实拍截图清单。

## 9. 待评审决策点

1. 玻璃主题是否跟随系统浅/深自动切换两套玻璃（方案默认：不跟随，`liquid` 固定深色玻璃）？
2. 是否接受 Windows 10 / Linux 的"纯色兜底"观感差异？
3. 历史窗、OCR 结果窗是否纳入玻璃范围（默认纳入 P1，P2 虚化按性能再定）？
4. P2 是否纳入本次迭代？

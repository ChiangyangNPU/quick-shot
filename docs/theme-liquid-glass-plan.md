# 液态玻璃主题设计方案

> 状态：已实施（v0.1.7）
> 适用范围：QuickShot 桌面端设置窗、工具栏、提示条、历史窗、OCR 结果窗
> 不适用：截图遮罩、贴图窗口、宠物窗口（理由见 §5）
>
> **实施评审结论**（相对初版方案的变更）：
> 1. 明暗适配：`liquid` 不再固定深色玻璃，而是**跟随系统明暗**实时解析浅色/深色两套玻璃（`themeMode()` 语义改为"外观"，liquid 时取 `QStyleHints::colorScheme()`）——所有既有深浅分支零改动生效；
> 2. 原生虚化（原 P2）并入本次交付：macOS `NSVisualEffectView` 全形态支持；Windows 仅大面板走 Win11 官方 DWM 亚克力（Win10 尽力用非公开接口），**无边框悬浮工具条在 Windows 上一律不做原生虚化**（layered 窗口与 DWM backdrop 互斥），由色板加浓兜底；
> 3. 内嵌子控件（截图/录屏工具栏、控制条）玻璃 = 取样父控件绘制内容做降采样磨砂模糊（`GlassPainter::sampleParentBackdrop`），纯 Qt 实现跨平台一致，观感即"对截图内容磨砂"，与 iOS 26 控件玻璃一致。

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
| macOS 12+ | 支持 | `NSVisualEffectView`（深色 HUD 用 `HUDWindow`、浅色用 `Popover`，面板用 `UnderWindowBackground`；`blendingMode behindWindow`）垫底，`kCACornerCurveContinuous` 连续圆角 | 接近系统原版 |
| Windows 11 22621+ | 大面板支持 | `DwmSetWindowAttribute(DWMWA_SYSTEMBACKDROP_TYPE, DWMSBT_TRANSIENTWINDOW)` + `DwmExtendFrameIntoClientArea` + 系统圆角（悬浮工具条是 layered 窗口，与 DWM backdrop 互斥，不做原生虚化） | 接近磨砂 |
| Windows 10 | 部分 | 仅大面板尝试非公开 `SetWindowCompositionAttribute` Acrylic；失败即纯色玻璃 | 磨砂偏灰 |
| Linux | 不支持 | 无系统合成器接口，回退为"纯色玻璃" | 玻璃渐变卡片 |

降级策略：`GlassBackdrop::blurSupported(Surface)` 按平台×形态探测（大面板/悬浮分开），`StyleManager::liquidPalette()` 据此对无虚化形态的玻璃底色加浓 alpha（≈232）保证可读性；单窗口挂载失败（`attach` 返回 false）时 `glassPanelColor()` 再做单窗口级兜底。不做弹窗提示。

## 7. 实施清单（已完成）

- 颜色表新增 liquid 套（深浅两套玻璃）+ 主题三值化（`s_theme`="liquid"）+ 设置页三选项 + 六语言（`style.theme.liquid`）
- 外观解析：`StyleManager::systemDark()/syncSystemAppearance()`（`QStyleHints::colorScheme()`，Unknown 回落调色板亮度），`themeMode()` 对 liquid 返回系统外观；`main.cpp` 连接 `colorSchemeChanged` 实现实时跟随
- 自绘玻璃：`src/widgets/GlassPainter.*`（玻璃底色 + 顶部 sheen + 上亮下暗内描边 + 1/8 降采样磨砂模糊 + `sampleParentBackdrop` 父控件取样）
- 平台桥：`src/platform/GlassBackdrop.*`（mac `NSVisualEffectView` / Win11 DWM 亚克力 + Win10 非公开接口兜底 / 其他 noop）
- 窗口接入：`BaseToolBar`（paintEvent 玻璃化 + 顶层 attach + 子工具栏换 `GlassPanelWidget`）、`RecordingControlWindow`、`ScrollCaptureSession` 提示条、`SettingsWindow::updateWindowBackground`、`HistoryWindow::updateWindowColors`、`OcrResultDialog`
- alpha 修复：`StyleManager::cssColor()` 取代窗口底色处的 `QColor::name()`（后者丢 alpha，玻璃透明度会失效）
- 单测：`tests/StyleManagerTest.cpp` 新增 liquid 三用例（setTheme 兼容回落、applyTheme 玻璃色板、refreshLiquidPalette 幂等）

## 8. 风险与验证状态

1. **性能**：系统虚化开销可控；自绘磨砂为 1/8 降采样（显示/移动时才重画），无逐行阴影。未采用 `QGraphicsDropShadowEffect`。
2. **半透明窗口与截图工具叠加**：设置窗开启玻璃时若背后是截图遮罩，虚化结果无意义；不做特殊处理，接受该场景观感下降。
3. **Windows 待实机验证**：Win11 DWM 亚克力 + 系统圆角、Win10 非公开接口、DPI 缩放下观感——CI 构建通过后需实机目检；失败场景自动落纯色玻璃兜底，不黑屏。
4. **配色回归**：颜色表 21 项被多处直接读取，玻璃色板 alpha 需按 WCAG AA（正文 4.5:1）核对；`MessageBox` 等不接虚化的窗口由 `getMessageBoxStyle` 内部抬升至近实色。
5. **测试覆盖**：主题管线由单测覆盖（liquid 三用例）；视觉观感靠人工回归——交付附浅色/深色/彩色壁纸三种背景下的实拍截图清单。

## 9. 决策记录

1. 玻璃主题跟随系统浅/深自动切换两套玻璃（**已采纳**，替代初版"固定深色"默认）。
2. Windows 10 / Linux 的"纯色兜底"观感差异：**接受**（可读性优先）。
3. 历史窗、OCR 结果窗纳入玻璃范围（**已纳入**）。
4. 原生虚化（原 P2）**并入本次迭代交付**。

# QuickShot 液态玻璃主题实施方案

## Context

用户希望给 QuickShot（Qt 6.10.2 Widgets 桌面应用）新增苹果 macOS 26 / iOS 26 "Liquid Glass" 风格的主题。仓库中已有一份设计文档 `docs/theme-liquid-glass-plan.md`（被暂存删除但未提交，v0.1.6 发布说明仍引用它，属误删，将恢复并更新）。

**已确认的决策**（用户选定）：
1. **范围**：按设计文档——设置窗、历史窗、OCR 结果窗、截图/录屏工具栏、子工具栏、录屏控制条、长截图提示条玻璃化；贴图窗、截图遮罩（SnipScreen）、宠物窗**不做**（截图颜色还原与选区精度优先）
2. **明暗**：液态玻璃跟随系统浅色/深色外观自动切换（最像苹果）
3. **Windows**：Win11 22H2+ 走官方 DWM 亚克力；Win10 用非公开 API 尽力，失败降级纯色玻璃

**核心思路**：现有主题管线（`StyleManager` 21 色 token 表 + `themePalette()` + `app.qss` `@占位符` + 设置页下拉）扩展第三主题 `"liquid"`。关键简化：让 `themeMode()` 语义变为"**外观**"（liquid 时解析为系统明暗），则全代码库几十处 `dark ? A : B` 分支几乎零改动即可正确生效；玻璃材质本身交给系统原生 backdrop，Qt 只画玻璃叠加层。

**明确不做**（Qt Widgets 技术边界，需 QML/着色器重写才可能）：动态折射/边缘透镜、控件液体形变流动动画。静态观感（真实背景虚化、连续圆角、顶部高光描边、内暗边、柔和投影）做到接近系统原版。

## 一、主题管线改造 — `src/core/StyleManager.h/.cpp`

1. **主题三值化**：`s_theme` 支持 `"light"|"dark"|"liquid"`；`setTheme()`/`applyTheme()` 接受 `"liquid"`（未知值仍回落 light，双向兼容）
2. **外观解析**：
   - 新增 `static bool s_systemDark`，由 Qt 6.10 `QGuiApplication::styleHints()->colorScheme()` 初始化（Unknown 时按 QPalette 窗口色亮度判断）
   - `themeMode()` 改为：`s_theme=="liquid"` 时返回系统外观（Dark/Light），否则按现逻辑 —— 所有既有二值分支（`toolbarIconTint`、`applyDarkTitleBar`、`getComboBoxStyle`、`getPathEditStyle` 等约 10 处）**无需修改**即随系统外观生效
   - 新增 `static bool isLiquid()`
3. **液态玻璃色板**：`themePalette(bool dark)` 旁新增 `liquidPalette(bool darkAppearance)`，覆盖 21 色中玻璃相关项（低 alpha 让原生模糊透出来）：
   - 深色玻璃：`ToolbarBg ≈ rgba(24,24,28,150)`、`ToolbarBtn ≈ rgba(255,255,255,45)`、hover 70、文字 `#f2f2f2`、`TabWidgetBg ≈ rgba(20,20,26,195)`
   - 浅色玻璃：`ToolbarBg ≈ rgba(255,255,255,120)`、按钮白 160/hover 220、文字 `#1d1d1f`、`TabWidgetBg ≈ rgba(250,250,252,205)`
   - `CaptureBorder/RecordBorder/Handle*/CloseButton*` 保持不随主题（现有约定）
4. **无原生模糊兜底**：新增 `static bool s_nativeBlur`（由平台桥报告）；不可用时 `liquidPalette()` 对面板底色做 alpha 提升（+60 左右）保证可读性
5. **全局 QSS**：`loadGlobalStyleSheet()` 的 token 哈希增加 liquid 分支（按外观取值）：`@viewBg`、`@groupBoxBorder`、`@inputBorder*`、`@scrollBg/Handle`、`@checkBox*`、`@separatorColor` 等给玻璃半透明值
6. `applyTheme()` 持久化逻辑不变（HexArgb），`style/theme=liquid` 落盘

## 二、自绘玻璃叠加层 — 新增 `src/widgets/GlassPainter.h/.cpp`

`GlassPainter::paintGlassPanel(QPainter*, QRectF, qreal radius, const QColor &fill)`：
- 圆角矩形填充（fill 来自 token）
- 1px 内描边：上半 `rgba(255,255,255,~115)→透明` 渐变（镜面高光），下半 `rgba(0,0,0,~45)`（内暗边）
- 顶部 40% 高度的白→透明 sheen 渐变
- `BaseToolBar::paintEvent`（`src/widgets/BaseToolBar.cpp:162`）改为：`isLiquid()` 时调用 GlassPainter，否则维持现状。子工具栏/标注工具栏共用基类，一处改动全生效

## 三、平台原生背景虚化桥 — 新增 `src/platform/GlassBackdrop.*`

```cpp
namespace GlassBackdrop {
  enum class Material { Panel, HUD };   // Panel=窗口大面板, HUD=悬浮工具条
  struct Options { Material material; qreal cornerRadius; };
  bool attach(QWidget*, const Options&); // 返回是否获得原生模糊
  void detach(QWidget*);
  bool nativeBlurAvailable();
}
```
- **`GlassBackdrop_mac.mm`**：沿用 `applyDarkTitleBar` 的 objc_msgSend 先例。取 NSWindow → 创建 `NSVisualEffectView`（material：HUD→`NSVisualEffectMaterialHUDWindow`，Panel→`NSVisualEffectMaterialUnderWindowBackground`；blendingMode `BehindWindow`；state Active）→ `wantsLayer` + `layer.cornerRadius` + **`kCACornerCurveContinuous`（苹果连续圆角，"跟苹果一样"的关键细节）** → `addSubview:positioned:NSWindowBelow` + 宽高 autoresize。句柄→view 存静态表供 detach。悬浮工具条本就 `WA_TranslucentBackground`，直接生效
- **`GlassBackdrop_win.cpp`**：
  - 探测：RtlGetVersion ≥ 10.0.22621 → Win11 官方路径：`DwmSetWindowAttribute(DWMWA_SYSTEMBACKDROP_TYPE=38, DWMSBT_TRANSIENTWINDOW)` + `DwmExtendFrameIntoClientArea(-1)` + `DWMWA_WINDOW_CORNER_PREFERENCE=ROUND` + `DWMWA_BORDER_COLOR=DWMWA_COLOR_NONE`；玻璃窗口需去掉 layered 属性（`WA_TranslucentBackground` 冲突，attach 内处理并记原状态供 detach 恢复）
  - Win10：`GetProcAddress(user32,"SetWindowCompositionAttribute")` + `ACCENT_ENABLE_ACRYLICBLURBEHIND`，失败返回 false → 调用方走兜底色板
  - mingw 头文件缺新常量则在 .cpp 内按值自行定义
- **`GlassBackdrop_other.cpp`**（Linux）：noop，`nativeBlurAvailable()=false`
- `CMakeLists.txt` 按平台选源文件（模仿 `ScreenRecorder_win/mac` 的既有写法，`CMakeLists.txt:360-373`）

## 四、窗口接入（attach/detach + 主题刷新）

| 窗口 | 文件 | 材质 |
|---|---|---|
| 截图/录屏工具栏、子工具栏、标注栏 | `BaseToolBar::initShell`（一处） | HUD |
| 录屏控制条 | `widgets/RecordingControlWindow.cpp` | HUD |
| 长截图提示条 | `capture/ScrollCaptureSession.cpp` | HUD |
| 设置窗 | `widgets/SettingsWindow.cpp` | Panel |
| 历史窗 | `widgets/HistoryWindow.cpp` | Panel |
| OCR 结果窗 | `ocr/OcrResultDialog.cpp` | Panel |

- 各窗口构造/主题切换（`refreshTheme`/`updateWindowColors` 等既有入口）时：liquid → `attach`，非 liquid → `detach`
- 设置/历史/OCR 为带标题栏的系统窗口：macOS 效果视图垫在 contentView 后，标题栏由 `applyDarkTitleBar`（现有）管外观；Windows DWM backdrop 天然覆盖全窗
- 明确不接：PinWindow、SnipScreen、宠物窗、TranslateOverlayWindow；MessageBox 不 attach 但经 token 自动获得玻璃配色

## 五、跟随系统明暗 + 设置页 + 翻译

1. `src/main.cpp`：连接 `QGuiApplication::styleHints()->colorSchemeChanged` → `StyleManager::updateSystemAppearance(dark)` → 若当前是 liquid：重铺色板 + `reapplyGlobalStyleSheet()` + 复用现有 `themeChanged` 广播链路（`main.cpp:224-247`）刷新各存活窗口 → 实现系统明暗切换时玻璃实时跟随
2. `src/widgets/settings/SettingsStyleTab.cpp`：`cmbTheme` 增加 `"liquid"` 项（`setupUi` 与 `retranslateUi` 两处，注意 `blockSignals` 惯例）；选 liquid 时同样走 `onThemeChanged` 现有流程
3. `src/languages/{zh_CN,zh_TW,zh_HK,en_US,ja_JP,ko_KR}.json`：新增 `style.theme.liquid`（液态玻璃 / Liquid Glass / リキッドガラス / 리퀴드 글래스 / 液態玻璃…）
4. 全库 grep 审计直接比较 `"dark"` 字符串的残留点（预期极少，`themeMode()` 语义化后基本消除）

## 六、文档与测试

- `git restore --staged docs/theme-liquid-glass-plan.md && git restore docs/theme-liquid-glass-plan.md` 恢复误删文档，更新：状态改为已实施、记录本次三项决策（跟随系统明暗、Win11 优先、范围）、P2 并入本次
- `tests/StyleManagerTest.cpp`：新增 liquid 主题用例（色板铺色、`themeMode()` 外观解析、setTheme 兼容回落）

## 实施顺序

1. StyleManager 主题管线（三值化 + 外观解析 + 色板 + QSS token）
2. GlassPainter 自绘叠加层
3. GlassBackdrop 平台桥（macOS → Win11 → Win10 兜底）
4. 窗口接入 + 设置页/翻译 + 系统明暗跟随
5. 文档恢复更新 + 测试

## 验证

- **macOS（本机）**：`cmake -S . -B build -DCMAKE_PREFIX_PATH=$(brew --prefix qt) && cmake --build build --parallel`，跑 `ctest`（开 `QUICKSHOT_BUILD_TESTS`）。运行后用系统截图/computer-use 实检：彩色壁纸上看设置窗/工具栏真实背景虚化 + 连续圆角 + 高光描边；系统外观切换（系统设置→浅色/深色）玻璃实时变化；三主题来回切换无残留；截图遮罩/贴图逐像素不受影响
- **Windows**：本机无法运行，构建交给 CI（`.github/workflows/ci.yml`）或用户实机；重点验 Win11 亚克力 + DWM 圆角、Win10 降级不黑底、文字对比度
- **回归**：浅色/深色主题外观与改动前一致；用户自定义单色的能力保留（颜色表行照常可改）
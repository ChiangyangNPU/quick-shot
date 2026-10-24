# 浅色/深色主题切换功能

## 背景（Context）

用户很喜欢滚动截图提示条的深色 HUD 样式（[ScrollCaptureSession.cpp](file:///e:/develop/Code/github_new/quick-shot/src/capture/ScrollCaptureSession.cpp) 里 `ScrollHintContainer`：底板 `rgba(22,22,26,248)`、1px 亮边、白字加粗、按钮 `rgba(255,255,255,48)` 填充），希望把它推广为整个应用的深色主题。

**需求（已与用户确认）：**
1. 设置界面"样式"选项卡新增主题下拉框：浅色 / 深色
2. 深色主题影响**整个应用 UI**：截屏/录屏/子工具栏/录屏控制栏 + 设置窗口、历史窗口、托盘菜单、MessageBox 等
3. 浅色 = 当前 UI 状态
4. 现有颜色自定义项**保留**：主题只是预设，切换时批量覆盖颜色表，之后仍可单项微调（主题下拉框仍显示当前主题，不出现"自定义"项）
5. **立即生效**：存活工具栏/窗口立即刷新

## 核心机制

利用 StyleManager 现有的 **21 项颜色数据驱动表**（`colorSettingTable()`）作为唯一数据源，主题 = 一组预设色板批量覆盖。主题标量持久化到 `style/theme`（"light"/"dark"），与 `s_toolbarButtonStyle` 同款路径。

切换主题调用链：

```
cmbTheme 切换 → StyleManager::applyTheme(theme, cm)
  ├─ 批量 setColor + 写全部颜色键 + 写 theme 键
  ├─ reapplyGlobalStyleSheet()（占位符重填值）
  ├─ SettingsStyleTab 刷新色块按钮/容器背景/各输入框样式
  └─ emit themeChanged → SettingsWindow 转发 → main.cpp 接线（main.cpp:222-232）：
      ├─ SnipScreen::refreshTheme()（刷 m_toolbar；RecordingSession::refreshTheme() 由
      │   SnipScreen 内部转发调用——SnipScreen.cpp:1987，刷录屏工具栏 + 控制窗口）
      ├─ HistoryWindow::updateWindowColors()（复用现有）
      ├─ PinWindow::refreshAllTheme()（新增静态槽）
      └─ trayMenu 立即重设样式 trayMenu->setStyleSheet(getMenuStyle())（main.cpp:229-232）
```

## 实现步骤

### 1. ThemeMode 与持久化（StyleManager.h/.cpp、ConfigManager.cpp）

- `StyleManager` 新增 `enum class ThemeMode { Light, Dark }`、`static QString s_theme`、`DEFAULT_THEME = "light"`、方法 `theme()/setTheme()/themeMode()/applyTheme(theme, cm)/toolbarIconTint()`
- `ConfigManager::scalarDefaults()` 追加 `style/theme` 默认值；`initFromConfig()` 末尾 `setTheme(...)`；`resetToDefaults()` 重置 `s_theme`
- 启动时无需按 theme 重铺色板（颜色键已被持久化覆盖，天然恢复）

### 2. 深色色板（StyleManager.cpp，按 StyleColorId 顺序）

| 项 | 深色值 | 说明 |
|---|---|---|
| ToolbarBg / SubToolbarBg / RecordControlBg | QColor(22,22,26,248) | HUD 底板 |
| ToolbarBtn | QColor(255,255,255,48) | 按钮填充 |
| ToolbarText / SettingButtonText / TabButtonText | #f2f2f2 | 文字 |
| ToolbarButtonHover | QColor(255,255,255,78) | hover |
| ToolbarButtonDisabled | QColor(255,255,255,20) | 禁用 |
| ToolbarButtonChecked | #0078d7 保留 | 深色下蓝色高亮清晰 |
| SettingButtonBg | QColor(255,255,255,48) | 设置页按钮填充（SettingButtonText 同步 #f2f2f2；StyleManager.cpp:292） |
| TabWidgetBg | #1a1a1e | 设置/历史主背景（实色） |
| TabButtonBg | QColor(255,255,255,30)；TabButtonSelectedBg | #23232a；TabButtonSelectedText | #ffffff |
| CaptureBorder / RecordBorder / HandleCircle / HandleClose / CloseButton* | 不变 | 屏幕叠加物/红色语义不随主题 |

### 3. alpha 通道序列化（关键风险点）

当前 `saveStyleSettings()` 与各样式方法用 `color.name()` 会**丢掉 alpha**。深色色板大量依赖 alpha，必须：

- StyleManager.cpp 新增内部工具 `qssColor(const QColor&)`：alpha<255 时输出 `rgba(r,g,b,a)`，否则 `name()`
- 所有拼 QSS 处（getToolbarBackgroundStyle/getSubToolbarStyle/getToolButtonStyle/getActionButtonStyle 等）统一改用 `qssColor()`
- `saveStyleSettings()` / `ConfigManager` 写颜色处改用 `name(QColor::HexArgb)`（Qt 读取端 `QColor("#AARRGGBB")` 兼容）

### 4. app.qss 主题化：占位符替换方案

硬编码颜色仅 6 类，改占位符共 **18 个**（`@textColor`、`@groupBoxBorder`、`@inputBorder`、`@inputBorderFocus`、`@scrollBg`、`@scrollHandle`、`@scrollHandleHover`、`@sliderGroove`、`@sliderHandle`、`@colorButtonBorder`、`@viewBg`、`@separatorColor`、`@checkBoxBorder`、`@checkBoxBg`、`@checkBoxBorderHover`、`@checkBoxChecked`、`@checkBoxDisabledBorder`、`@checkBoxDisabledBg`，见 StyleManager.cpp:644-661），避免两份 qss 的重复维护。

- StyleManager.cpp 新增内部 `loadGlobalStyleSheet()`：读 `:/stylesheets/app.qss`，按 `themeMode()` 替换占位符（浅色用原值；深色用 `#f2f2f2`、`rgba(255,255,255,55/80/160)`、`#2a2a2e/#5a5a60/#7a7a80`、`#3a3a3e/#c0c0c4` 等）
- `reapplyGlobalStyleSheet()` 与 main.cpp 启动加载处统一走 `loadGlobalStyleSheet()`

### 5. get*Style() 硬编码"白底黑字"主题化

对 `getPathEditStyle/getKeySequenceEditStyle/getSettingsCheckBoxStyle/getSettingsComboBoxStyle/getGroupBoxStyle/getMenuStyle/getMessageBoxStyle/getOcrResultTextStyle/getColorButtonStyle/getComboBoxStyle` 加 `const bool dark = (themeMode()==ThemeMode::Dark)` 分支填值（深色：底 `#1a1a1e`/`#2a2a2e`、字 `#f2f2f2`、边 `rgba(255,255,255,55~160)`）。这些派生色不进 colorSettingTable，避免表膨胀。

### 6. SVG 图标染色

- `loadSvgIcon` 新增带 `const QColor& tint` 的重载：render 后 `CompositionMode_SourceIn` + `fillRect(tint)`；原重载委托新版传 `QColor()` 不染色，不破坏现有调用
- 新增 `toolbarIconTint()`：深色返回 `#f2f2f2`，浅色返回无效色
- 调用点：[BaseToolBarActions.cpp](file:///e:/develop/Code/github_new/quick-shot/src/widgets/BaseToolBarActions.cpp#L195)、[RecordingControlWindow.cpp](file:///e:/develop/Code/github_new/quick-shot/src/widgets/RecordingControlWindow.cpp#L345-L357) 4 处加 tint（L345/349/353/357）
- `getSettingsComboBoxStyle()` 里 QSS `image:` 引用的 spinner-up/down.svg 无法动态染色：新增白色版 `spinner-up-dark.svg`/`spinner-down-dark.svg`（注册进 qrc），深色分支换 URL

### 7. SettingsStyleTab UI

- 新增 `m_themeGroup`（标题 `style.themeGroup`/"外观"）放在滚动区最顶部，内含标签 + `cmbTheme` 下拉框（照搬 `cmbToolbarButtonStyle` 模式：addItem 带 data、`useHighlightOnlyPopup`、`getSettingsComboBoxStyle()`）
- `onThemeChanged(int)` 槽：`applyTheme` → 刷新色块按钮 → `reapplyGlobalStyleSheet()` → `refreshStyles()` + `applyColorPostUpdate(TabWidgetBg,...)` + 各 ComboBox/SpinBox/Button 重设样式 → `emit themeChanged(theme)`
- `retranslateUi()` 仿现有下拉框 blockSignals 重建；`loadStyleSettings()` 同步当前主题
- `onResetStyle()` 追加：主题下拉框重置为浅色、写 theme 键、`reapplyGlobalStyleSheet()`、`emit themeChanged`

### 8. 信号链路与各窗口刷新

- StyleManager 是静态类不能发信号：`SettingsStyleTab::themeChanged` → `SettingsWindow::themeChanged`（转发）→ main.cpp `ensureSettingsWindow` 内接线
- 新增槽：`SnipScreen::refreshTheme()`（m_toolbar 重设 `getToolbarBackgroundStyle()` + 按钮样式/图标重建）、`RecordingSession::refreshTheme()`（同理 + m_controlWindow 调已存在的 `updateBackgroundStyle()/updateButtonStyles()`）、`PinWindow::refreshAllTheme()`（仿 `refreshAllToolDefaults` 静态遍历）、`SettingsWindow::refreshAllStyles()`（循环调各 Tab 的 `refreshStyles()` + 自身背景）
- HistoryWindow 复用现有 `updateWindowColors()`；托盘菜单在 `themeChanged` 时由 main.cpp **立即**重设样式 `trayMenu->setStyleSheet(StyleManager::getMenuStyle())`（main.cpp:229-232），非等下次弹出时自动取新值

### 9. 翻译（src/languages/*.json × 6）

新增 `style.themeGroup`（外观/Appearance）、`style.theme`（主题:/Theme:）、`style.theme.light`（浅色/Light）、`style.theme.dark`（深色/Dark），zh_TW/zh_HK/ja_JP/ko_KR 同步译文。

### 10. 文档（按用户规则更新）

- `docs/architecture.md`：StyleManager 章节加"主题模式"小节（ThemeMode/applyTheme 与颜色表关系、占位符 qss、信号链）
- `README.md` / `README-EN.md`：功能特性加主题切换说明
- 新增方法全部补中文 Doxygen 注释（`@brief/@param/@return/@note/@author chiangyang`），重要段落加 `LOG_INFO`（英文）

### 11. 深色标题栏（applyDarkTitleBar）

- `StyleManager::applyDarkTitleBar(WId windowId)`（StyleManager.h:183-190）：Windows 经 DWM 沉浸式深色标题栏（`DwmSetWindowAttribute` + `DWMWA_USE_IMMERSIVE_DARK_MODE`），macOS 经 `NSWindow.appearance`（DarkAqua/Aqua），其他平台空实现
- 各窗口创建后与主题切换时一并应用，使原生标题栏随主题同步变深/变浅

## 验证（用户自行编译后人工验证）

1. 浅色默认无回归：删配置启动，UI 与改造前一致
2. 切深色立即生效：设置/历史窗口、托盘菜单、MessageBox 变深；截图工具栏、录屏工具栏、子工具栏、录屏控制栏呈深色 HUD；图标模式按钮图标染白
3. 持久化：重启后深色恢复；配置含 `style/theme=dark` 且颜色键为 HexArgb 深色值
4. 微调兼容：深色下单改某颜色立即生效且重启保留；主题下拉框仍显示"深色"
5. 切回浅色 + 恢复默认：全部回浅色默认，主题下拉框显示"浅色"
6. 回归：DPI 变化后 qss 重应用仍深色；深色下切换按钮文字/图标模式正常；SpinBox 深色箭头可见

## 主要风险

1. **alpha 序列化**（步骤 3）：遗漏会导致重启后深色变实色/样式串失效，最高优先处理
2. spinner 箭头资源遗漏会导致深色下 SpinBox 箭头不可见
3. TabWidgetBg 与 TabButtonSelectedBg 深色下过于接近，靠 SelectedText 加亮区分（#23232a 错开层级）

# 标注工具支持虚线绘制

## Context

标注（矩形、椭圆、三角形、直线、箭头、画笔）原先在 [Annotation.cpp](file:///e:/develop/Code/github_new/quick-shot/src/capture/Annotation.cpp) 的 `Annotation::draw()` 中统一使用 `Qt::SolidLine`，无法绘制虚线。需求：这些几何标注工具在绘制时可切换为虚线，且每个工具独立记忆、重启后保持。

最终实现：为每个工具增加独立的「实线/虚线」开关（沿用 `ToolSettings` 的每工具独立设置机制），开关位于线宽滑块与颜色面板之间；选择写入配置持久化。

## 涉及文件与实现

### 1. 标注数据层

**[Annotation.h](file:///e:/develop/Code/github_new/quick-shot/src/capture/Annotation.h)**
- `Annotation` 构造函数新增可选参数 `Qt::PenStyle penStyle = Qt::SolidLine`，新增成员 `Qt::PenStyle m_penStyle` 与只读访问器 `penStyle()`。
- `RectAnnotation`、`EllipseAnnotation`、`TriangleAnnotation`、`LineAnnotation`、`ArrowAnnotation`、`PenAnnotation` 的构造函数追加同名可选参数并透传基类（默认实线，兼容既有调用点）。

**[Annotation.cpp](file:///e:/develop/Code/github_new/quick-shot/src/capture/Annotation.cpp)**
- `draw()` 中 `QPen` 的样式由固定 `Qt::SolidLine` 改为 `m_penStyle`；抗锯齿、RoundCap/RoundJoin 不变，虚线间隔随 `penWidth` 自动缩放。
- 箭头头部三角形仍为实心填充，只有主干线为虚线。

### 2. 交互层数据流

**[AnnotationInteractionHandler.h](file:///e:/develop/Code/github_new/quick-shot/src/capture/AnnotationInteractionHandler.h)**
- 新增 `setDashed(bool)` / `isDashed()` 与成员 `bool m_currentDashed = false;`，风格与 `setColor` / `setPenWidth` 一致。

**[AnnotationInteractionHandler.cpp](file:///e:/develop/Code/github_new/quick-shot/src/capture/AnnotationInteractionHandler.cpp)**
- `createAnnotation()` 为矩形、椭圆、三角形、箭头、画笔、直线六类标注传入 `m_currentDashed ? Qt::DashLine : Qt::SolidLine`；文本、马赛克、橡皮不受影响。

### 3. 工具栏 UI 与持久化

**[BaseToolBar.h](file:///e:/develop/Code/github_new/quick-shot/src/widgets/BaseToolBar.h)**
- `ToolSettings` 新增 `bool dashed = false;`（每工具独立记忆）。
- `SubToolVariantData` 新增 `QPushButton *styleBtn = nullptr;`。
- 新增信号 `void penStyleChanged(bool dashed);`。
- 新增私有辅助方法：`QPushButton *makeLineStyleButton(QWidget *parent)`、`void updateLineStyleButtonText(QPushButton *btn, bool dashed)`、`bool loadDashedSetting(int toolId) const`、`void saveDashedSetting(int toolId, bool dashed) const`。

**[BaseToolBar.cpp](file:///e:/develop/Code/github_new/quick-shot/src/widgets/BaseToolBar.cpp)**
- `buildSubVariant()` 的 `VariantRectangle` 与 `VariantColorPen` 分支（线宽滑块之后、颜色面板之前）添加可选中的线型按钮，样式复用 `StyleManager::getActionButtonStyle()`；点击时写回 `m_toolSettings[m_currentToolId].dashed`、写入配置并 `emit penStyleChanged(checked)`。
- 静态函数 `dashedConfigKey(toolId)` 将工具映射到配置键；文本/马赛克/橡皮返回空键，不持久化。
- `syncVariantState()`：同步按钮 `isChecked()` 与文案（blockSignals 防重入），恢复各工具各自记忆的状态。
- `showSubTools()`：矩形/椭圆/箭头/画笔/直线分支在发射线宽/颜色信号后追加 `emit penStyleChanged(ts.dashed)`。
- `retranslateUi()`：语言切换时刷新两个变体中按钮的文案与 tooltip。

**[BaseToolBarActions.cpp](file:///e:/develop/Code/github_new/quick-shot/src/widgets/BaseToolBarActions.cpp)**
- `createAnnotationTools()` 初始化每个工具的 `ToolSettings` 时调用 `loadDashedSetting(def.toolId)` 恢复上次选择。

### 4. 持久化

**[ConfigManager.cpp](file:///e:/develop/Code/github_new/quick-shot/src/core/ConfigManager.cpp)** 默认配置新增 5 个键（老配置启动时自动补齐，默认 `false`=实线）：

- `style/dashedRectangle`（矩形，三角形共用）
- `style/dashedEllipse`
- `style/dashedArrow`
- `style/dashedPen`
- `style/dashedLine`

### 5. 宿主接线

- [SnipScreen.cpp](file:///e:/develop/Code/github_new/quick-shot/src/capture/SnipScreen.cpp)：`penStyleChanged → m_annotationHandler->setDashed()`，并打英文日志。
- [PinWindow.cpp](file:///e:/develop/Code/github_new/quick-shot/src/widgets/PinWindow.cpp)：同上。
- [RecordingSession.cpp](file:///e:/develop/Code/github_new/quick-shot/src/capture/RecordingSession.cpp)：同上。

### 6. 多语言

`src/languages/{zh_CN,zh_TW,zh_HK,en_US,ja_JP,ko_KR}.json` 在 `shape.*` 附近新增：

- `lineStyle.solid`：实线 / 實線 / Solid / 実線 / 실선
- `lineStyle.dashed`：虚线 / 虛線 / Dashed / 破線 / 점선

## 设计要点

- 虚线开关作用于矩形/三角形（共用 `VariantRectangle` 变体）与椭圆/箭头/画笔/直线（共用 `VariantColorPen` 变体）；文本、马赛克、橡皮不显示该开关。
- 线型由 `Annotation` 对象自身持有 `m_penStyle`，撤销/重做、贴图缩放与既有绘制流程无需额外改动。
- 各工具的线型偏好相互独立，写入 `QuickShot.ini` 的 `style/dashed*`，新开标注窗口与重启后保持。
- 默认实线，不改变既有行为。

## 验证

（用户自行编译）
1. 截图界面 → 矩形工具 → 子工具栏点击「实线/虚线」切到虚线 → 绘制矩形/椭圆/三角形，确认虚线样式。
2. 直线、箭头、画笔分别切虚线绘制，确认箭头主干虚线、头部实心。
3. 切换不同工具再切回，确认各工具线型开关独立记忆；撤销/重做后虚线样式仍在。
4. 贴图窗口（PinWindow）与录屏标注工具栏重复上述验证。
5. 重启软件后再次进入，确认各工具保持上次的实线/虚线选择。
6. 切换语言（中/英），确认按钮文案与 tooltip 刷新正确。

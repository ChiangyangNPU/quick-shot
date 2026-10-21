# 标注工具支持虚线绘制

## Context

当前所有标注（矩形、椭圆、三角形、直线、箭头、画笔）在 [Annotation.cpp](file:///e:/develop/Code/github_new/quick-shot/src/capture/Annotation.cpp#L30-L38) 的 `Annotation::draw()` 中统一使用 `Qt::SolidLine`，用户无法绘制虚线标注。需求：这些几何标注工具在绘制时可选择虚线形式。

方案：为每个工具增加独立的「实线/虚线」开关（沿用现有 `ToolSettings` 的每工具独立设置机制），开关位于颜色/粗细子工具栏中，切换工具后各自记忆。

## 涉及文件与修改

### 1. 标注数据层

**[Annotation.h](file:///e:/develop/Code/github_new/quick-shot/src/capture/Annotation.h)**
- `Annotation` 构造函数末尾新增可选参数 `Qt::PenStyle penStyle = Qt::SolidLine`，新增成员 `Qt::PenStyle m_penStyle` 与只读访问器 `penStyle()`。
- 为 `RectAnnotation`、`EllipseAnnotation`、`TriangleAnnotation`、`LineAnnotation`、`ArrowAnnotation`、`PenAnnotation` 的构造函数追加同名可选参数并透传给基类（默认实线，保持既有调用点兼容）。
- 为新增参数补 Doxygen 注释（@param penStyle）。

**[Annotation.cpp](file:///e:/develop/Code/github_new/quick-shot/src/capture/Annotation.cpp#L30-L38)**
- `draw()` 中 `QPen` 的样式由固定 `Qt::SolidLine` 改为 `m_penStyle`。抗锯齿、RoundCap/RoundJoin 保持不变；Qt 虚线间隔随 penWidth 自动缩放。
- 箭头头部三角形仍为实心填充，只有主干线为虚线（视觉上箭头指向清晰）。

### 2. 交互层数据流

**[AnnotationInteractionHandler.h](file:///e:/develop/Code/github_new/quick-shot/src/capture/AnnotationInteractionHandler.h#L366-L371)**
- 新增 `setDashed(bool)` / `isDashed()` 与成员 `bool m_currentDashed = false;`，风格与 `setColor`/`setPenWidth` 一致。

**[AnnotationInteractionHandler.cpp](file:///e:/develop/Code/github_new/quick-shot/src/capture/AnnotationInteractionHandler.cpp#L502-L545)**
- `createAnnotation()` 中为矩形、椭圆、三角形、箭头、画笔、直线六类标注传入 `m_currentDashed ? Qt::DashLine : Qt::SolidLine`；文本/马赛克/橡皮保持不变。

### 3. 工具栏 UI

**[BaseToolBar.h](file:///e:/develop/Code/github_new/quick-shot/src/widgets/BaseToolBar.h)**
- `ToolSettings` 新增 `bool dashed = false;`（每个工具独立记忆）。
- `SubToolVariantData` 新增 `QPushButton *styleBtn = nullptr;`。
- 新增信号 `void penStyleChanged(bool dashed);`。
- 新增私有辅助方法：`QPushButton *makeLineStyleButton(QWidget *parent)`、`void updateLineStyleButtonText(QPushButton *btn, bool dashed)`。

**[BaseToolBar.cpp](file:///e:/develop/Code/github_new/quick-shot/src/widgets/BaseToolBar.cpp)**
- `buildSubVariant()` 的 `VariantRectangle` 与 `VariantColorPen` 分支（线宽滑块之后、颜色面板之前）添加可勾选按钮，样式复用 `StyleManager::getActionButtonStyle()`（支持 `:checked` 高亮）；点击时写回 `m_toolSettings[m_currentToolId].dashed` 并 `emit penStyleChanged(checked)`。
- `syncVariantState()`：同步按钮 `isChecked()` 与文案（blockSignals 防重入），恢复各工具各自记忆的状态。
- `showSubTools()`：矩形/椭圆/箭头/画笔/直线分支在发射线宽/颜色信号后追加 `emit penStyleChanged(ts.dashed)`。
- `retranslateUi()`：语言切换时刷新两个变体中按钮的文案与 tooltip。

### 4. 宿主接线

- [SnipScreen.cpp](file:///e:/develop/Code/github_new/quick-shot/src/capture/SnipScreen.cpp#L287-L298)：`penStyleChanged → m_annotationHandler->setDashed()`，并打英文日志。
- [PinWindow.cpp](file:///e:/develop/Code/github_new/quick-shot/src/widgets/PinWindow.cpp#L709-L717)：同上。
- [RecordingSession.cpp](file:///e:/develop/Code/github_new/quick-shot/src/capture/RecordingSession.cpp#L172-L180)：同上。

### 5. 多语言

`src/languages/{zh_CN,zh_TW,zh_HK,en_US,ja_JP,ko_KR}.json` 在 `shape.*` 附近新增：
- `lineStyle.solid`：实线 / 實線 / Solid / 実線 / 실선
- `lineStyle.dashed`：虚线 / 虛線 / Dashed / 破線 / 점선

## 设计要点

- 虚线开关对矩形/椭圆/三角形（共用 Rectangle 变体）与椭圆/箭头/画笔/直线（共用 ColorPen 变体）生效；文本、马赛克、橡皮不显示该开关。
- 通过 `Annotation` 对象自身持有 `m_penStyle`，撤销/重做、贴图缩放、已有绘制流程无需额外改动。
- 默认实线，不改变现有行为；不属于持久化设置项。

## 验证

（用户自行编译）建议验证路径：
1. 截图界面 → 矩形工具 → 子工具栏点击「实线/虚线」切换到虚线 → 绘制矩形/椭圆/三角形，确认虚线样式。
2. 直线、箭头、画笔分别切虚线绘制，确认箭头主干虚线、头部实心。
3. 切换不同工具再切回，确认各工具虚线开关独立记忆；撤销/重做后虚线样式仍在。
4. 贴图窗口（PinWindow）与录屏标注工具栏重复上述验证。
5. 切换语言（设置中切换中/英），确认按钮文案刷新正确。

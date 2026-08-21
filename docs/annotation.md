# QuickShot 标注系统技术文档

## 1. 概述

QuickShot 的标注系统是跨场景复用的图形标注功能，支持**截屏**（SnipScreen）、**录屏**（SnipScreen 实时 overlay 合成）、**贴图**（PinWindow）三大使用场景。通过 `AnnotationInteractionHandler` Mixin 工具类将标注交互逻辑统一到一处，两处窗口通过注入 Host 回调实现差异化。

标注系统提供 8 种工具、完整的撤销/重做/清除栈、修饰键约束（Shift/Alt）、控制点精调、马赛克像素化、局部橡皮擦、光标样式反馈等全部功能，录屏中每帧同步 overlay，确保绘制过程与最终结果完整出现在视频里。

### 1.1 工具清单

| 工具 | ToolId | 控制点 | 支持拖动 | 说明 |
|---|---|---|---|---|
| 矩形 | Rectangle=0 | 8 个（4 角 + 4 边中点） | ✅ | 框选区域 |
| 椭圆 | Ellipse=1 | 4 个（左/右/上/下） | ✅ | 圈选区域 |
| 箭头 | Arrow=2 | 2 个（起点/终点） | ✅ | 指向性标注 |
| 画笔 | Pen=3 | — | ✅ | 自由书写 |
| 直线 | Line=4 | 2 个（起点/终点） | ✅ | 连接两点 |
| 文本 | Text=5 | — | ✅ | 输入文字，字号/颜色可调 |
| 马赛克 | Mosaic=6 | — | ❌ | 对敏感信息做像素化模糊（块大小可全局配置） |
| 橡皮擦 | Eraser=7 | — | ❌ | 局部擦除任意标注笔迹（支持撤销） |

### 1.2 快捷键

标注快捷键由 `AnnotationShortcutController` 统一管理，SnipScreen 与 PinWindow 均实现 `IShortcutHandler` 接口作为策略。画笔粗细范围 1-20（`kMinPenWidth`/`kMaxPenWidth`），与子工具栏滑块联动。

| 快捷键 | 动作 | 说明 |
|---|---|---|
| `1`-`8` | 切换标注工具 | 依次对应矩形/椭圆/箭头/画笔/直线/文本/马赛克/橡皮擦 |
| `Ctrl+Z` | 撤销 | 优先级：清除 → 移动 → 最后一步操作 |
| `Ctrl+Y` / `Ctrl+Shift+Z` | 重做 | 优先级同上 |
| `Delete` / `Backspace` | 清除全部标注 | 可通过撤销恢复 |
| `[` | 画笔宽度 -1 | 与马赛克块、橡皮擦半径联动 |
| `]` | 画笔宽度 +1 | |
| `Tab` | 循环切换颜色 | 默认顺序：红→蓝→黑→黄→绿→白→红 |
| `Esc` | 取消当前操作 | 优先级：文本编辑 → 取消录制 → 退出截图/贴图 |

文本编辑框获得焦点时，以上裸键（数字键、`[`/`]`、`Tab`、`Esc`、`Delete`/`Backspace`）会被临时禁用，避免与文本输入冲突。

---

## 2. 整体架构

### 2.1 分层架构图

```mermaid
graph TB
    subgraph HostLayer [宿主窗口层]
        SS[SnipScreen 截屏与录屏]
        PW[PinWindow 贴图窗口标注]
    end

    subgraph ShortcutLayer [快捷键策略层]
        ISH[IShortcutHandler 接口]
        ASC[AnnotationShortcutController 统一QShortcut注册 裸键批量启用禁用]
    end

    subgraph InteractionLayer [交互逻辑层]
        AIH[AnnotationInteractionHandler Mixin工具类 统一鼠标交互与绘制调度]
    end

    subgraph DataLayer [数据层]
        AM[AnnotationManager 命令模式撤销栈与擦除马赛克笔迹]
        ANNO[Annotation类家族 矩形椭圆三角箭头直线画笔文本]
        ERASER[EraserGroups 按笔迹分组矩形序列]
        MOSAIC[MosaicGroups 按笔迹分组矩形序列]
    end

    subgraph RecordingLayer [录屏合成层]
        SCR[ScreenRecorder 每帧取overlay合成]
        RAO[renderAnnotationOverlay 生成输出分辨率QImage]
    end

    SS -->|实现接口| ISH
    PW -->|实现接口| ISH
    ISH <-->|策略引用| ASC
    SS -->|持有并注入Host回调| AIH
    PW -->|持有并注入Host回调| AIH
    AIH -->|持有| AM
    AM -->|存放标注对象| ANNO
    AM -->|存放擦除笔迹| ERASER
    AM -->|存放马赛克笔迹| MOSAIC
    AIH -->|Host.syncOverlay| RAO
    RAO -->|setAnnotationOverlay| SCR
```

### 2.2 核心类图

```mermaid
classDiagram
    class Annotation {
        <<abstract>>
        -AnnotationType m_type
        -QPoint m_start
        -QPoint m_end
        -QColor m_color
        -int m_penWidth
        +AnnotationType type()
        +QPoint start()
        +QPoint end()
        +QRect rect()
        +QColor color()
        +int penWidth()
        +void setStart(QPoint)
        +void setEnd(QPoint)
        +void translate(QPoint)
        +void scale(double, double)
        +bool hitTest(QPoint)
        +hasControlPoints() bool
        +controlPointCursor(int) QtCursorShape
        +void draw(QPainter&)
    }

    class RectAnnotation {
        +controlPoints 八点 四角加四边中点
        +moveControlPoint 角点对角调整 边中点单轴调整
        +controlPointCursor 斜双箭头或单轴光标
        +hitTest 边框距离容差算法
    }

    class EllipseAnnotation {
        +controlPoints 四点 左右上下
        +moveControlPoint 单边移动对边固定
        +controlPointCursor 左右水平上下垂直光标
        +hitTest 轮廓加粗命中检测
    }

    class TriangleAnnotation {
        +controlPoints 三个顶点
        +moveControlPoint 独立顶点自由变形
        +controlPointCursor SizeAllCursor四向箭头
        +setEnd 同步等腰三角形第三顶点
    }

    class ArrowAnnotation {
        +controlPoints 起点与终点
        +moveControlPoint 调整箭尾与箭头尖
    }

    class LineAnnotation {
        +controlPoints 起点与终点
        +moveControlPoint 调整线段两端
    }

    class PenAnnotation {
        +translate 平移全部路径点
        +scale 缩放全部路径点
        +addPoint 绘制中追加路径点
    }

    class TextAnnotation {
        +scale 同步缩放字号
    }

    class AnnotationManager {
        +static int kDefaultMosaicBlockSize equals 7
        +void add(AnnotationUPtr)
        +void updateLast(QPoint)
        +void commit()
        +void rollback()
        +void undo()
        +void redo()
        +void clear()
        +void draw(QPainter&)
        +void drawMosaic(QPainter&, QPixmap, int, QPoint)
        +void drawControlPoints(QPainter&)
        +Annotation* lastAnnotation()
        +int hitControlPoint(QPoint, int)
        +QtCursorShape controlPointCursorAt(QPoint)
        +void moveLastControlPoint(int, QPoint)
        +void beginMove()
        +void translateLast(QPoint)
        +void endMove()
        +void scaleAll(double, double)
        +void eraseAt(QPoint, int)
        +void endEraserStroke()
        +void mosaicAt(QPoint, int)
        +void endMosaicStroke()
    }

    class AnnotationInteractionHandler {
        -AnnotationManager m_annotationManager
        -AnnotationType m_currentTool
        -QColor m_currentColor
        -int m_currentPenWidth
        -int m_currentFontSize
        -int m_currentShapeType
        -bool m_isAnnotating
        -bool m_isErasing
        -bool m_isMosing
        -bool m_isDraggingAnnotation
        -bool m_isDraggingControlPoint
        +void setHost(HostStruct)
        +bool handleMousePress(QPoint, QtModifiers)
        +bool handleMouseMove(QPoint, QtModifiers)
        +bool handleMouseRelease()
        +void drawAnnotations(QPainter&, QPoint)
        +void drawWithMosaic(QPainter&, QPixmap&, int, QPoint)
        +void createAnnotation(QPoint)
        +void exitAnnotation()
        +AnnotationType tool()
        +void setTool(AnnotationType)
        +int penWidth()
        +void setPenWidth(int)
        +QColor color()
        +void setColor(QColor)
        +int fontSize()
        +void setFontSize(int)
        +int shapeType()
        +void setShapeType(int)
        +AnnotationManager& manager()
    }

    note for AnnotationInteractionHandler "Host回调共12项  clampPos isInSelection isInToolBar selectionRect requestUpdate syncOverlay updateToolBarState createTextEdit finalizeTextEdit activeTextEdit setCursor setBareKeysEnabled"

    class AnnotationShortcutController {
        +static int kMinPenWidth equals 1
        +static int kMaxPenWidth equals 20
        +AnnotationShortcutController(QWidget*, IShortcutHandler*)
        +void setBareKeysEnabled(bool)
    }

    class IShortcutHandler {
        <<interface>>
        +bool canAnnotate()
        +void onToolSwitch(int)
        +void onCopy()
        +void onSave()
        +void onUndo()
        +void onRedo()
        +void onClear()
        +void onPenWidthChange(int)
        +void onCycleColor()
        +void onCancel()
        +void onRefresh()
    }

    class SnipScreen {
        +paintEvent
        +eventFilter 转发给Handler
        +renderAnnotationOverlay 生成QImage
        +pushAnnotationOverlay
        +updateToolBarState
    }

    class PinWindow {
        +paintEvent
        +鼠标事件转发给Handler
        +scaleAll缩放标注
        +compositePixmap导出含标注图像
    }

    Annotation <|-- RectAnnotation
    Annotation <|-- EllipseAnnotation
    Annotation <|-- TriangleAnnotation
    Annotation <|-- ArrowAnnotation
    Annotation <|-- LineAnnotation
    Annotation <|-- PenAnnotation
    Annotation <|-- TextAnnotation
    AnnotationManager o-- Annotation
    AnnotationInteractionHandler o-- AnnotationManager
    SnipScreen ..|> IShortcutHandler
    PinWindow  ..|> IShortcutHandler
    AnnotationShortcutController --> IShortcutHandler
    SnipScreen o-- AnnotationInteractionHandler
    PinWindow  o-- AnnotationInteractionHandler
    SnipScreen o-- AnnotationShortcutController
    PinWindow  o-- AnnotationShortcutController
```

### 2.3 相关文件清单

| 文件 | 说明 |
|---|---|
| `src/capture/Annotation.h` | 标注基类 + 7 个子类 + 控制点/工具枚举 |
| `src/capture/AnnotationManager.h` | 标注管理器（撤销栈、马赛克、橡皮擦、控制点） |
| `src/capture/AnnotationInteractionHandler.h` | Mixin 交互处理器 + Host 回调接口 |
| `src/capture/AnnotationInteractionHandler.cpp` | 鼠标事件、绘制调度、标注创建逻辑 |
| `src/shortcut/AnnotationShortcutController.h` | 标注快捷键控制器（策略模式，基于 QShortcut） |
| `src/shortcut/IShortcutHandler.h` | 宿主窗口需实现的快捷键动作接口 |
| `src/widgets/OverlayTextEdit.h` | 文本工具使用的独立编辑框 |
| `src/capture/SnipScreen.cpp` | 截屏/录屏宿主（注入 Host 回调） |
| `src/widgets/PinWindow.cpp` | 贴图宿主（注入 Host 回调，支持滚轮缩放标注） |

---

## 3. 核心组件详解

### 3.1 Annotation 标注类型体系

**设计模式：模板方法模式**。基类 `Annotation` 负责通用绘制调度（`draw()` 设置统一的画笔属性后调用虚函数 `drawAnnotation()`）和通用属性（start/end/color/penWidth）；子类只需实现 `drawAnnotation()` 即可，可选重写 `controlPoints()`、`moveControlPoint()`、`hitTest()`、`translate()`、`scale()` 等以支持控制点、特殊命中检测、缩放同步等高级能力。

控制点采用 **`ControlPoint{ pos, role }`** 结构，`role` 由子类自定义，`hitControlPoint(pos, 6)` 做命中检测（绘制半径 5px，容差 6px 便于点击）。控制点绘制样式统一为 **白填充 + 灰色边框 5px 圆**（`drawControlPoints()`）。

### 3.2 AnnotationManager（命令模式）

`AnnotationManager` 是纯数据层，不依赖 Qt 窗口，可独立复用于任何场景。内部维护**多条操作序列**，并通过全局操作序号（`nextOpSeq`）统一比较撤销优先级。

#### 操作序列与撤销优先级

| 操作类型 | 数据结构 | 提交函数 |
|---|---|---|
| 标注创建/修改 | `undoStack` / `redoStack` | `commit()` 清空 redo |
| 标注整体移动 | `undoMoveState` / `redoMoveState`（MoveRecord） | `endMove()` 保留原始位置使 undo 生效 |
| 橡皮擦笔迹 | `eraserGroups` / `redoEraserGroups`（按 Stroke 分组） | `endEraserStroke()` |
| 马赛克笔迹 | `mosaicGroups` / `redoMosaicGroups`（按 Stroke 分组） | `endMosaicStroke()` |
| 整体清除 | `clearUndoSnapshot` / `clearRedoSnapshot`（完整快照） | `clear()` |

#### undo() 决策流程图

```mermaid
graph TD
    SU([调用撤销]) --> A{清除快照有效}
    A -->|是| B[恢复清除快照到容器 清除快照失效]
    A -->|否| C{移动记录有效且撤销栈非空}
    C -->|是| D[撤销栈顶标注恢复到移动前位置 移动记录失效]
    C -->|否| E[比较撤销栈顶 擦除组 马赛克组操作序号 撤销最大者]
```

#### redo() 决策流程图

```mermaid
graph TD
    SR([调用重做]) --> A{清除重做快照有效}
    A -->|是| B[恢复清除重做快照到容器 重做快照失效]
    A -->|否| C{移动重做记录有效且撤销栈非空}
    C -->|是| D[撤销栈顶标注恢复到移动后位置 移动重做记录失效]
    C -->|否| E[比较重做栈顶 重做擦除组 重做马赛克组 重做最小序号]
```

#### 马赛克绘制流程图（drawMosaicRects）

```mermaid
graph LR
    S([输入矩形列表]) --> N{逐个矩形处理}
    N -->|取下一个| C1[背景裁剪该区域]
    C1 --> C2[缩小到块大小分之一]
    C2 --> C3[放大回原尺寸像素化]
    C3 --> C4[生成像素化图像]
    C4 --> C5[画回原位置]
    C5 --> N
    N -->|处理完毕| D([局部遮挡完成])
```

马赛克按 Stroke 以"一组矩形块"存储（每帧鼠标位置的 bounding rect，大小为画笔宽度乘 2）。
常量 `AnnotationManager::kDefaultMosaicBlockSize = 7`，一处修改全局生效。`blockSize` 越大模糊越强（3 为轻度，7 为默认，10 以上为强遮挡）。

#### 橡皮擦实现

橡皮擦不修改标注对象，而是在 `draw()` 时用 `QRegion::subtracted()` 把 eraserRegion 从 `clipRegion` 中扣除，达到**局部擦除笔迹**的视觉效果。擦除范围同样为画笔宽度乘 2 的一系列矩形。

---

### 3.3 AnnotationInteractionHandler（Mixin 工具类）

统一 SnipScreen 和 PinWindow 高度重复的标注交互。内部持有 `AnnotationManager` 实例和一组**属性状态**（tool/color/penWidth/fontSize/shapeType）。

#### Host 回调接口

Host 为 12 个 `std::function` 回调，用于在不引入继承耦合的前提下解决两处窗口差异：

| 回调 | SnipScreen 实现 | PinWindow 实现 |
|---|---|---|
| `clampPos(pos)` | 限制到选区矩形内 | 限制到窗口 rect 内 |
| `isInSelection(pos)` | `m_selector->selected().contains()` | 恒 true（窗口即选区） |
| `isInToolBar(pos)` | `isMouseInToolBar(pos)`（防止点击工具栏被当作标注） | 恒 false（独立顶层工具栏） |
| `selectionRect()` | `m_selector->selected()` | `rect()` |
| `requestUpdate()` | `update()` | `update()` |
| `syncOverlay()` | `pushAnnotationOverlay()`（录屏每帧同步 overlay） | 空（无录屏） |
| `updateToolBarState(u, r)` | `updateToolBarState(u, r)` + 录屏工具栏同步 | `updateToolBarState()` |
| `createTextEdit(globalPos)` | 创建 OverlayTextEdit，全局→本地坐标，设置边界 | 创建 OverlayTextEdit，窗口本地坐标 |
| `finalizeTextEdit()` | 提取文本 → 创建 TextAnnotation → 清理 | 提取文本 → 创建 TextAnnotation → 清理 |
| `activeTextEdit()` | 返回 `m_textEdit` | 返回 `m_textEdit` |
| `setCursor(shape)` | `m_selector->setCursor(shape)` | `setCursor(shape)` |
| `setBareKeysEnabled(e)` | 启用/禁用标注快捷键裸键 | 启用/禁用标注快捷键裸键 |

#### handleMousePress 交互优先级流程图

```mermaid
graph TD
    P([鼠标按下]) --> A{命中工具栏}
    A -->|是| R([返回假 工具栏处理])
    A -->|否| B{存在文本编辑框}
    B -->|是且点击外部| C[完成文本编辑 刷新 返回真]
    B -->|是且点击内部| D([返回假 编辑框处理])
    B -->|否| E{工具是橡皮擦}
    E -->|是且在选区内| F[擦除加状态刷新加录屏同步 返回真]
    E -->|否| G{工具是马赛克}
    G -->|是且在选区内| H[马赛克涂抹 刷新 返回真]
    G -->|否| I{在选区内且命中控制点}
    I -->|是| J[记录控制点索引 返回真]
    I -->|否| K{工具是文本}
    K -->|是且在选区内| K1{命中标注}
    K1 -->|是| L1[开始整体拖动 返回真]
    K1 -->|否| M1[调用宿主创建文本框 返回真]
    K -->|否| N{在选区内且命中标注且可拖动}
    N -->|是| L2[开始整体拖动 返回真]
    N -->|否| O{在选区内}
    O -->|是| P2[创建新标注 返回真]
    O -->|否| Q([返回假])
```

#### handleMouseMove 交互优先级流程图

```mermaid
graph TD
    M([鼠标移动]) --> A{正在拖拽控制点}
    A -->|是| B[限制到选区 更新控制点 刷新 同步录屏叠加]
    A -->|否| C{正在拖动标注}
    C -->|是| D[限制到选区 计算位移 平移标注 刷新 同步叠加]
    C -->|否| E{正在擦除}
    E -->|是| F[限制到选区 执行擦除 更新撤销状态 刷新 同步叠加]
    E -->|否| G{正在涂抹马赛克}
    G -->|是| H[限制到选区 执行马赛克 刷新 同步叠加]
    G -->|否| I{正在绘制标注}
    I -->|是| J{工具是画笔}
    J -->|是| J1[追加路径点]
    J -->|否| J2[约束后更新起止点]
    J1 --> K[刷新 同步录屏叠加]
    J2 --> K
    I -->|否| L[光标反馈 控制点光标优先 命中标注四向箭头否则十字]
```

---

### 3.4 AnnotationShortcutController（策略模式）

解决两处窗口重复实现快捷键，并修复 PinWindow 原先使用 keyPressEvent 导致的焦点时序 bug：
- SnipScreen/PinWindow 均实现 `IShortcutHandler` 作为策略
- 控制器以 `parent` 为宿主在构造时注册全部 `QShortcut`，不再依赖 keyPressEvent
- `setBareKeysEnabled(bool)` 批量启用/禁用裸键快捷键，文本编辑框创建时禁用，完成后恢复
- `canAnnotate()` 前置检查统一在控制器完成，宿主无需每个动作重复判断

#### 生命周期时序图

```mermaid
sequenceDiagram
    participant SS as SnipScreen
    participant PW as PinWindow
    participant ASC as AnnotationShortcutController
    note over SS,PW: 应用启动阶段
    SS->>ASC: 创建控制器传入父窗口和策略接口
    ASC->>ASC: 注册全部QShortcut 撤销重做宽度颜色删除数字键等
    note over PW: 用户打开贴图窗口
    PW->>PW: 进入标注模式
    PW->>ASC: 创建控制器传入父窗口和策略接口
    ASC->>ASC: 注册全部QShortcut
    note over PW: 用户创建文本框
    PW->>ASC: 禁用裸键快捷键
    ASC->>ASC: 遍历裸键列表逐个设为禁用
    note over PW: 用户完成文本编辑
    PW->>ASC: 启用裸键快捷键
    ASC->>ASC: 遍历裸键列表逐个设为启用
    note over PW: 用户退出标注模式
    PW->>PW: 退出标注模式
    PW->>ASC: 销毁控制器
    note over SS: 应用退出阶段
    SS->>ASC: 成员变量自动析构销毁
```

生命周期总结：
- SnipScreen：构造时创建（成员变量），析构时销毁
- PinWindow：`enterAnnotationMode` 创建（堆对象），`exitAnnotationMode` 销毁

---

## 4. 绘制流程

### 4.1 常规绘制时序图（无马赛克/橡皮擦）

```mermaid
sequenceDiagram
    participant Host as 宿主窗口
    participant AIH as 交互处理器
    participant AM as 标注管理器
    participant QP as 绘图设备
    Host->>QP: 步骤一 绘制背景图
    Host->>AIH: 步骤二 调用drawAnnotations传入绘图设备与偏移
    AIH->>QP: 步骤三 保存当前状态加坐标系偏移
    AIH->>AM: 步骤四 调用draw传入绘图设备
    loop 遍历撤销栈中每个标注
        AM->>QP: 逐个调用annotation.draw
        note over AM: 若有擦除笔迹则设置裁剪区域扣除擦除部分
    end
    AIH->>AM: 步骤五 绘制控制点
    AM->>AM: 判断栈顶标注是否支持控制点
    AM->>QP: 绘制白色填充加灰色边框圆点控制点
    AIH->>QP: 步骤六 恢复之前保存的绘图状态
```

### 4.2 马赛克/橡皮擦绘制（离屏 canvas）流程图

由于马赛克需要对已经画了标注和背景的图像做采样像素化，且橡皮擦使用 clipRegion 扣减，必须先渲染到离屏 QPixmap 再一次性画回窗口。控制点在马赛克之后绘制，确保其白灰圆点清晰可见不被像素化模糊。

```mermaid
graph LR
    S([绘制事件触发]) --> A[建同尺寸离屏像素图透明填充]
    A --> B[建离屏绘制器绑定像素图]
    B --> C[离屏画布上绘制背景图]
    C --> D[调用马赛克绘制方法 传入块大小和偏移]
    D --> E[平移坐标系对齐标注]
    E --> F[绘制全部标注 先扣除擦除区域]
    F --> G[绘制马赛克效果 采样像素化]
    G --> H[最后绘制控制点 保持清晰]
    H --> I[结束离屏绘制 释放资源]
    I --> J[窗口一次性合成整张画布]
```

### 4.3 录屏 overlay 同步时序图（renderAnnotationOverlay）

录屏视频只允许标注通过 overlay 合成，为此通过 SetWindowDisplayAffinity 加 WDA_EXCLUDEFROMCAPTURE 将 SnipScreen 排除出 BitBlt 捕获。每次状态变化都调用 syncOverlay 刷新 overlay 图像，录屏器每帧合成。

```mermaid
sequenceDiagram
    participant User as 用户操作
    participant Host as SnipScreen
    participant AIH as 交互处理器
    participant RAO as 渲染overlay函数
    participant AM as 标注管理器
    participant SCR as 录屏器
    participant Win as 平台录屏线程
    note over Host: 录屏开始请求
    Host->>Host: 调用SetWindowDisplayAffinity排除窗口捕获
    Host->>SCR: 调用start传入捕获区域和输出分辨率
    Host->>RAO: 推送标注overlay
    RAO->>SCR: 存入新生成的QImage
    loop 每帧录制循环
        SCR->>Win: 加锁取annotationOverlay
        Win->>Win: BitBlt捕获桌面 已排除SnipScreen窗口
        Win->>Win: drawImage合成overlay到帧缓冲
        Win->>SCR: writeSample写入编码管线
    end
    note over User: 绘制矩形按住拖动
    User->>Host: mousePress转发eventFilter
    Host->>AIH: handleMousePress传入坐标
    AIH->>AIH: createAnnotation进入绘制中状态
    loop 每次mouseMove拖动过程
        User->>Host: mouseMove
        Host->>AIH: handleMouseMove传入坐标和修饰键
        AIH->>AM: updateLast更新标注或者画笔追加点
        AIH->>Host: syncOverlay同步录屏overlay
        Host->>RAO: pushAnnotationOverlay渲染新overlay
        RAO->>RAO: 坐标平移加缩放然后绘制标注
        RAO->>RAO: 马赛克区域裁剪背景缩放像素化
        RAO->>SCR: 存入更新后的QImage
        note over Win: 下一帧自动合成新标注 过程完整
    end
    User->>Host: mouseRelease松开
    Host->>AIH: handleMouseRelease
    AIH->>AM: commit提交清空重做栈
    AIH->>Host: syncOverlay同步最终结果
    Host->>RAO: pushAnnotationOverlay
    RAO->>SCR: 存入最终QImage
```

关键坐标对齐：`renderAnnotationOverlay` 内部使用的捕获区域等于选区减去边框宽度，与 `ScreenRecorder::start` 传入的捕获矩形完全一致，消除边框宽度带来的标注偏移。

---

## 5. 关键技术点

### 5.1 Shift 与 Alt 修饰键约束

`AnnotationManager::applyModifierConstraints` 在 handleMouseMove 绘制分支中调用：
- **Shift 等比**：矩形和椭圆约束为正方形或圆形 直线箭头约束为45度倍数
- **Alt 中心**：以鼠标按下点为锚点向两侧对称展开
- **Shift加Alt**：两种约束可以组合使用
- **边界限制**：最终起止点被限制在选区内 防止标注超出选区或窗口

### 5.2 控制点与光标映射

| 标注 | 控制点数 | 光标映射 |
|---|---|---|
| RectAnnotation | 8（TL/TR/BL/BR / T/B/L/R） | 角点斜双箭头光标 边中点单轴光标 |
| EllipseAnnotation | 4（左右上下） | 左右为水平光标 上下为垂直光标 |
| TriangleAnnotation | 3（三个顶点） | 四向箭头光标 |
| ArrowAnnotation | 2（起点终点） | 四向箭头光标 |
| LineAnnotation | 2（起点终点） | 四向箭头光标 |

悬停在标注边框命中但非控制点时统一显示四向箭头光标表示可整体拖动。马赛克与橡皮擦工具悬停始终显示十字光标。

### 5.3 PinWindow 标注缩放同步 scaleAll

PinWindow 滚轮缩放窗口时所有标注必须随图像同步缩放否则标注偏离原位置。调用 `manager().scaleAll(sx, sy)`：
- 每个标注调用 scale 矩形类缩放起止点 画笔额外缩放路径点 文本额外缩放字号
- 画笔宽度按平均缩放 保证视觉一致
- 马赛克与橡皮擦矩形笔迹同步缩放 保证遮挡与擦除区域匹配

### 5.4 录屏过程完整性

每个操作的 Move 阶段和 Release 阶段都会调用 syncOverlay 确保视频中的过程与窗口绘制一致。

| 操作 | Move 拖动过程 | Release 结束提交 | 修复前遗留问题 |
|---|---|---|---|
| 控制点拖拽 | 已同步 | 已同步 | 无 |
| 标注整体拖动 | 已同步 | 已同步 | 修复前拖动过程未显示 |
| 橡皮擦擦除 | 已同步 | 已同步 | 无 |
| 马赛克涂抹 | 已同步 | 已同步 | 无 |
| 绘制矩形椭圆箭头直线画笔 | 已同步 | 已同步 | 修复前绘制过程未显示只看到最终结果 |

---

## 6. 扩展指南 如何新增一种标注类型

### 新增 StarAnnotation 流程图

```mermaid
graph LR
    S1([新增五角星子类继承标注基类]) --> S2([重写绘制方法 可选支持控制点])
    S2 --> S3([实现五个顶点控制点和拖动与光标])
    S3 --> S4([重写命中检测与缩放平移同步顶点])
    S4 --> S5([标注类型枚举加五角星成员 编号为八])
    S5 --> S6([交互处理器创建标注时新增五角星分支])
    S6 --> S7([三类工具栏加五角星按钮 toolId设为八])
    S7 --> S8([快捷键注册加数字键九的快捷方式])
    S8 --> S9([八项验证 绘制约束拖动擦除 撤销重做清除导出])
```

### 验证清单（步骤 9）

1. **绘制**：鼠标按下拖动松开五角星正常出现
2. **控制点调节**：五个控制点可拖拽变形 撤销可以恢复到原始形态
3. **修饰键约束**：绘制中 Shift 与 Alt 行为表现合理符合直觉
4. **拖动**：悬停显示四向箭头光标 拖动中位置实时同步
5. **橡皮擦**：局部擦除五角星边线 擦除笔迹可正常撤销恢复
6. **撤销重做清除**：整体清除后可通过撤销恢复 三层优先级表现正常
7. **导出**：截屏复制与保存 贴图复制与保存中的五角星形状均正确
8. **录屏overlay同步**：录制过程中绘制五角星过程完整可见 结果不重复不偏移

---

## 7. 修复历史

| 提交 | 说明 |
|---|---|
| ca951f5 | 创建 AnnotationInteractionHandler 提取公共标注交互逻辑 阶段一 |
| e8eb7d0 | SnipScreen 接入 Handler 净减少约三百二十二行 阶段二 |
| ba69e55 | PinWindow 接入 Handler 净减少约二百四十八行 阶段三 |
| a8843a8 | 修复录屏视频中标注重复显示 排除SnipScreen窗口捕获加overlay坐标与边框对齐 |
| 08e0c2e | 修复录屏过程中绘制与拖动过程不显示 补充handleMouseMove分支中两处syncOverlay调用 |

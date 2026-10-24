# 桌面宠物功能实现计划（已实现）

> 状态：**已实现**。本文档原为实现计划，实际落地多处超出原设计；正文已按现有代码改写，计划外新增见文末「实现超出计划的部分」一节。

## Context（背景）

用户希望为 quick-shot 增加"桌面宠物"趣味功能：

1. 截图后点击**复制**或**贴图**时，截图图像从截图区域以飞行动画传到桌面宠物嘴边，宠物张嘴把图像"吃掉"
2. 点击桌面宠物，桌面上出现一条**晾衣绳**，上面挂着每一次截图和复制的文字（复用现有历史记录数据）
3. 设置窗口中有一个**独立的"桌面宠物"选项卡**，内含开关，打开后宠物显示在桌面上并可用鼠标拖动位置

已确认的设计决策：
- 宠物形象用 **QPainter 代码绘制**（闭嘴/张嘴/咀嚼多态，无图片素材）
- 设置项为**独立的"桌面宠物"选项卡**（参照 SettingsHistoryTab 模式）
- 点击晾衣绳上的项目 → **复制到剪贴板**（与历史窗口行为一致）

## 现状关键代码

- 统一分发口：[CaptureSession.h](file:///e:/develop/Code/github_new/quick-shot/src/capture/CaptureSession.h) `publish()` —— 复制/贴图都经过它
- 挂钩点：[SnipScreen.cpp](file:///e:/develop/Code/github_new/quick-shot/src/capture/SnipScreen.cpp) 的 `copy()`（L564）与 `pinPixmap()`（L644），两处都能拿到截图选区的全局坐标（`snip()` 返回 pos；pinPixmap 内有 safePos/windowSize）
- 晾衣绳数据源：[HistoryManager](file:///e:/develop/Code/github_new/quick-shot/src/history/HistoryManager.h)（SQLite），`getItems(HistoryType::All, 0, N)` 取截图+文本，`itemAdded` 信号实时刷新；文本历史由 ClipboardMonitor 写入
- 无边框置顶窗口参考：[PinWindow.cpp](file:///e:/develop/Code/github_new/quick-shot/src/widgets/PinWindow.cpp) L124 `FramelessWindowHint | WindowStaysOnTopHint | Tool`
- 设置选项卡参考：[SettingsHistoryTab.cpp](file:///e:/develop/Code/github_new/quick-shot/src/widgets/settings/SettingsHistoryTab.cpp)（构造 setupUi + loadSettings + retranslateUi + refreshStyles 模式）
- 选项卡注册：[SettingsWindow.cpp](file:///e:/develop/Code/github_new/quick-shot/src/widgets/SettingsWindow.cpp) `setupHistoryTab()` L586（现为薄封装：构造 [SettingsHistoryTab](file:///e:/develop/Code/github_new/quick-shot/src/widgets/settings/SettingsHistoryTab.cpp) + addTab，历史选项卡的 UI/翻译/样式已内聚到 SettingsHistoryTab 类）
- 启动初始化：[main.cpp](file:///e:/develop/Code/github_new/quick-shot/src/main.cpp) `HistoryManager::instance()` L168、`DesktopPetManager::instance()->initFromConfig()` L171
- 构建：CMakeLists.txt **显式列出源文件**（无 GLOB），新文件需手动添加
- i18n：`src/languages/` 下 6 个 JSON（zh_CN/zh_HK/zh_TW/en_US/ja_JP/ko_KR）

## 实现步骤

### 1. 新建 `src/pet/` 模块（计划 4 个新类；实际落地 6 个组件，见「实现超出计划的部分」）

**DesktopPetManager（单例）** — `DesktopPetManager.h/.cpp`
- `instance()` / `setEnabled(bool)` / `isEnabled()` / `initFromConfig()`
- 配置持久化经 `ConfigManager`：`pet/enabled`（默认 false）、`pet/posX`/`pet/posY`（拖动后保存位置）
- `feed(const QPixmap &pixmap, const QRect &sourceGlobalRect)`：宠物未启用时直接返回；否则让宠物张嘴，并创建 FlyAnimationWidget 执行飞行动画
- 持有 PetWindow 与 ClotheslineWindow；转发宠物 `clicked()` → 切换晾衣绳显隐

**PetWindow** — `PetWindow.h/.cpp`
- 无边框透明置顶工具窗口（flags 同 PinWindow），固定约 96×96 逻辑像素
- QPainter 绘制圆滚滚卡通宠物：idle（闭嘴眨眼）、mouthOpen（张嘴待食）、chew（咀嚼缩放弹跳）三态；用 `Q_PROPERTY qreal mouthOpen / chewScale` + QPropertyAnimation 驱动，眨眼用 QTimer
- 拖动：mousePress/Move/Release 实现，移动距离超阈值判定为拖动；悬停 `Qt::OpenHandCursor`、拖动 `Qt::ClosedHandCursor`；拖动结束保存位置
- 单击（非拖动）发射 `clicked()`；提供 `mouthCenterGlobal()` 供飞行动画定位终点
- 出场默认位置：主屏幕右下角内收边距；配置里有位置则用配置

**FlyAnimationWidget** — `FlyAnimationWidget.h/.cpp`
- 无边框透明、`WindowTransparentForInput` 的置顶窗口，绘制圆角缩略图+轻微阴影
- `start(pixmap, fromGlobalRect, toGlobalPoint)`：QPropertyAnimation 驱动 geometry（起始矩形等比缩到 ≤220 逻辑像素 → 终点嘴边缩到 **18px**），QEasingCurve::**InCubic**，**480ms**；结束发射 `arrived()` 并 deleteLater（FlyAnimationWidget.cpp:12-14,67）
- 纯逻辑坐标驱动 geometry 即可，内部绘制按 devicePixelRatio 处理清晰度（项目为手动 DPI 策略）

**ClotheslineWindow** — `ClotheslineWindow.h/.cpp`
- 无边框透明置顶窗口：`showFullWidth()` 以**铺满主屏幕宽度的横条**显示，固定在屏幕顶部往下 1/5 高度处，与宠物位置无关（ClotheslineWindow.h:15-16、ClotheslineWindow.cpp:88 showFullWidth）
- QPainter 绘制一条**二次贝塞尔曲线近似**的晾衣绳（ClotheslineWindow.cpp:193），绳下用衣夹挂卡片：截图项显示缩略图（`HistoryItem::thumbnailPath` 加载），文本项显示前 **120 字符**摘要（`QTextOption::WrapAtWordBoundaryOrAnywhere` 自动换行绘制，ClotheslineWindow.cpp:352-358）；最多取最近 12 条（`getItems(HistoryType::All, 0, 12)`）
- 打开时卡片做入场下落+轻微摆动动画（QTimer 驱动相位角，paintEvent 里按 sin 偏转绘制）
- 单击卡片：截图 → 从 `content` 路径读原图 `clipboard->setPixmap()`；文本 → `setText()`；卡片弹跳一下作为反馈
- 关闭：右上角 **× 按钮**、**双击空白区域**、Esc、再次点击宠物、或点击窗口外（qApp eventFilter 监听全局鼠标按下，落在晾衣绳与宠物之外则关闭；对截图/录屏覆盖层豁免——截图复制/贴图/快照后晾衣绳保持打开并自动刷新最新记录）（ClotheslineWindow.h:20-22）
- 连接 `HistoryManager::itemAdded` / `itemRemoved` / `cleared` 实时刷新

### 2. 动画挂钩（SnipScreen 两处）

- `SnipScreen::copy()`：`CaptureSession::publish()` 后调用 `DesktopPetManager::instance()->feed(pixmap, QRect(pos, pixmap.size()))`（SnipScreen.cpp:572，直接用 pixmap 尺寸构造起点矩形，**未做 devicePixelRatio 换算**）
- `SnipScreen::pinPixmap()`：publish 后 `feed(pixmap, QRect(safePos, windowSize))`（SnipScreen.cpp:686）
- 实际共 3 处调用：上述两处之外，录屏工具栏**快照**复制后同样投喂（[RecordingSession.cpp](file:///e:/develop/Code/github_new/quick-shot/src/capture/RecordingSession.cpp) L238）；全屏/活动窗口截图、PinWindow 内复制未接

### 3. 设置选项卡

- 新建 `src/widgets/settings/SettingsPetTab.h/.cpp`（模式照抄 SettingsHistoryTab）：
  - 「打开桌面宠物」QCheckBox → `DesktopPetManager::setEnabled()`（即时显隐+持久化）
  - 「重置宠物位置」QPushButton → 清除 `pet/posX/posY` 并移回默认右下角
  - 一句功能说明 QLabel
- [SettingsWindow.cpp](file:///e:/develop/Code/github_new/quick-shot/src/widgets/SettingsWindow.cpp)：新增 `setupPetTab()`（addTab），并在 `retranslateUi()`、`updateAllGroupBoxStyles()` 中接线新选项卡；选项卡顺序放在 History 之后

### 4. 启动初始化

- [main.cpp](file:///e:/develop/Code/github_new/quick-shot/src/main.cpp) `HistoryManager::instance()`（L168）之后：`DesktopPetManager::instance()->initFromConfig()`（L171，配置开启则显示宠物）

### 5. 构建与 i18n

- CMakeLists.txt：add_executable 源文件清单加入 pet 模块文件（实际 11 个：5 个类的 .cpp/.h + PetUiScale.h，CMakeLists.txt:259-269）
- 6 个语言 JSON 增加键：`pet.*` 共 **13 键** —— `pet.tabName`、`pet.enable`、`pet.resetPosition`、`pet.description`、`pet.group`、`pet.empty`、`pet.styleLabel`、`pet.style.cat/shiba/panda/bunny/penguin/frog`（见 zh_CN.json）

### 6. 代码规范

- 全部中文 Doxygen 注释（@brief/@param/@return/@author chiangyang），风格与现有一致
- 关键路径加 `LOG_INFO`（英文日志）

## 实现超出计划的部分

以下能力为计划外新增（实现已落地，超出原设计）：

- **6 种宠物外观样式**：猫/柴犬/熊猫/兔子/企鹅/青蛙（`PetWindow::PetStyle` 枚举，[PetWindow.h](file:///e:/develop/Code/github_new/quick-shot/src/pet/PetWindow.h) L35-48）；配置键 `pet/style`（[ConfigManager.cpp](file:///e:/develop/Code/github_new/quick-shot/src/core/ConfigManager.cpp) L150）；设置页样式选择为平铺预览图按钮，点击即切换（[SettingsPetTab.cpp](file:///e:/develop/Code/github_new/quick-shot/src/widgets/settings/SettingsPetTab.cpp) L69-90）
- **文字投喂 `feedText()`**：监听 `HistoryManager::itemAdded`，文本记录触发文字卡片飞入宠物并被吃掉（[DesktopPetManager.h](file:///e:/develop/Code/github_new/quick-shot/src/pet/DesktopPetManager.h) L67-74、DesktopPetManager.cpp L49-52 接线）
- **两个计划外类**：`CardPreviewPopup`（晾衣绳卡片悬停放大预览：右键菜单复制/标注/OCR/翻译，滚轮 0.2-10 倍缩放）与 `PetUiScale`（宠物模块 DPI 缩放工具）；模块实际为 **6 个组件（5 个类 + PetUiScale 工具头，11 个源文件）**而非计划的 4 类
- **晾衣绳额外能力**：拖动空白区域移动窗口、四角与上下边拖拽调整大小、点击卡片弹跳反馈 + 呼吸高亮描边（ClotheslineWindow.h:18-23,104-112,155-156、ClotheslineWindow.cpp:239）、预览原图缓存上限 24（`kFullImageCacheLimit`，ClotheslineWindow.cpp:52）

## 验证

用户自行编译。功能验证清单：
1. 设置 → 桌面宠物选项卡 → 勾选开关 → 宠物出现在右下角；取消勾选 → 消失；重启应用状态保持
2. 拖动宠物换位，重启后位置保持
3. 截图 → 复制：图像从选区飞入宠物，宠物张嘴→咀嚼→闭嘴
4. 截图 → 贴图：同上动画
5. 点击宠物 → 晾衣绳出现（主屏全宽横条），展示最近的截图缩略图与复制文本；单击卡片 → 剪贴板内容被替换并弹跳反馈；× 按钮/双击空白/Esc/点窗外/再点宠物 → 关闭
6. 宠物关闭时截图复制：无动画、无异常
7. 多屏/DPI 缩放下动画起点与宠物位置正确

# 桌面宠物功能实现计划

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
- 挂钩点：[SnipScreen.cpp](file:///e:/develop/Code/github_new/quick-shot/src/capture/SnipScreen.cpp) 的 `copy()`（L562）与 `pinPixmap()`（L637），两处都能拿到截图选区的全局坐标（`snip()` 返回 pos；pinPixmap 内有 safePos/windowSize）
- 晾衣绳数据源：[HistoryManager](file:///e:/develop/Code/github_new/quick-shot/src/history/HistoryManager.h)（SQLite），`getItems(HistoryType::All, 0, N)` 取截图+文本，`itemAdded` 信号实时刷新；文本历史由 ClipboardMonitor 写入
- 无边框置顶窗口参考：[PinWindow.cpp](file:///e:/develop/Code/github_new/quick-shot/src/widgets/PinWindow.cpp) L124 `FramelessWindowHint | WindowStaysOnTopHint | Tool`
- 设置选项卡参考：[SettingsHistoryTab.cpp](file:///e:/develop/Code/github_new/quick-shot/src/widgets/settings/SettingsHistoryTab.cpp)（构造 setupUi + loadSettings + retranslateUi + refreshStyles 模式）
- 选项卡注册：[SettingsWindow.cpp](file:///e:/develop/Code/github_new/quick-shot/src/widgets/SettingsWindow.cpp) `setupHistoryTab()` L576（addTab + retranslateUi L374 + updateAllGroupBoxStyles L589）
- 启动初始化：[main.cpp](file:///e:/develop/Code/github_new/quick-shot/src/main.cpp) L167 附近
- 构建：CMakeLists.txt **显式列出源文件**（无 GLOB），新文件需手动添加
- i18n：`src/languages/` 下 6 个 JSON（zh_CN/zh_HK/zh_TW/en_US/ja_JP/ko_KR）

## 实现步骤

### 1. 新建 `src/pet/` 模块（4 个新类）

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
- `start(pixmap, fromGlobalRect, toGlobalPoint)`：QPropertyAnimation 驱动 geometry（起始矩形等比缩到适中大小 → 终点嘴边缩到 ~20px），QEasingCurve::InQuad，约 450ms；结束发射 `arrived()` 并 deleteLater
- 纯逻辑坐标驱动 geometry 即可，内部绘制按 devicePixelRatio 处理清晰度（项目为手动 DPI 策略）

**ClotheslineWindow** — `ClotheslineWindow.h/.cpp`
- 无边框透明置顶窗口，出现在宠物上方（空间不足则下方/旁边）
- QPainter 绘制一条悬链线晾衣绳，绳下用衣夹挂卡片：截图项显示缩略图（`HistoryItem::thumbnailPath` 加载），文本项显示前 3 行文字摘要；最多取最近 12 条（`getItems(HistoryType::All, 0, 12)`）
- 打开时卡片做入场下落+轻微摆动动画（QTimer 驱动相位角，paintEvent 里按 sin 偏转绘制）
- 单击卡片：截图 → 从 `content` 路径读原图 `clipboard->setPixmap()`；文本 → `setText()`；卡片弹跳一下作为反馈
- 关闭：Esc、再次点击宠物、或点击窗口外（qApp eventFilter 监听全局鼠标按下，落在晾衣绳与宠物之外则关闭）
- 连接 `HistoryManager::itemAdded` / `itemRemoved` / `cleared` 实时刷新

### 2. 动画挂钩（SnipScreen 两处）

- `SnipScreen::copy()`：`CaptureSession::publish()` 后调用 `DesktopPetManager::instance()->feed(pixmap, QRect(pos, pixmap.devicePixelRatio() 换算后的逻辑尺寸))`
- `SnipScreen::pinPixmap()`：publish 后 `feed(pixmap, QRect(safePos, windowSize))`
- 仅接这两处（用户明确描述的"复制/贴图"按钮）；全屏/活动窗口截图、PinWindow 内复制本期不接

### 3. 设置选项卡

- 新建 `src/widgets/settings/SettingsPetTab.h/.cpp`（模式照抄 SettingsHistoryTab）：
  - 「打开桌面宠物」QCheckBox → `DesktopPetManager::setEnabled()`（即时显隐+持久化）
  - 「重置宠物位置」QPushButton → 清除 `pet/posX/posY` 并移回默认右下角
  - 一句功能说明 QLabel
- [SettingsWindow.cpp](file:///e:/develop/Code/github_new/quick-shot/src/widgets/SettingsWindow.cpp)：新增 `setupPetTab()`（addTab），并在 `retranslateUi()`、`updateAllGroupBoxStyles()` 中接线新选项卡；选项卡顺序放在 History 之后

### 4. 启动初始化

- [main.cpp](file:///e:/develop/Code/github_new/quick-shot/src/main.cpp) L167 `HistoryManager::instance()` 之后：`DesktopPetManager::instance()->initFromConfig()`（配置开启则显示宠物）

### 5. 构建与 i18n

- CMakeLists.txt：add_executable 源文件清单中加入 4 个新 .cpp
- 6 个语言 JSON 增加键：`pet.tabName`、`pet.enable`、`pet.resetPosition`、`pet.description`

### 6. 代码规范

- 全部中文 Doxygen 注释（@brief/@param/@return/@author chiangyang），风格与现有一致
- 关键路径加 `LOG_INFO`（英文日志）

## 验证

用户自行编译。功能验证清单：
1. 设置 → 桌面宠物选项卡 → 勾选开关 → 宠物出现在右下角；取消勾选 → 消失；重启应用状态保持
2. 拖动宠物换位，重启后位置保持
3. 截图 → 复制：图像从选区飞入宠物，宠物张嘴→咀嚼→闭嘴
4. 截图 → 贴图：同上动画
5. 点击宠物 → 晾衣绳出现，展示最近的截图缩略图与复制文本；单击卡片 → 剪贴板内容被替换；Esc/点窗外/再点宠物 → 关闭
6. 宠物关闭时截图复制：无动画、无异常
7. 多屏/DPI 缩放下动画起点与宠物位置正确

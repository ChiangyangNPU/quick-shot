# 桌面宠物陪伴功能方案

## 一、需求概述

在现有桌面宠物（外观样式、投喂、晾衣绳、待办气泡）基础上，分三个阶段扩展陪伴能力：

1. **提醒与效率**：待办逾期/到期红点提醒；番茄钟/休息提醒。
2. **趣味陪伴**：待机动作（打瞌睡、张望）、互动反馈（摸头弹跳、拖动摇晃、投喂满足）。
3. **系统状态**：CPU/内存占用表情、低电量提醒。

## 二、设计原则

- **复用优先**：持久化统一走 `ConfigManager`（QSettings）；颜色与图标统一取 `PetTheme` /
  `StyleManager`；尺寸缩放统一走 `PetUi::scaled`；气泡/卡片复用 `TodoWindow` 的自绘方案
  （圆角面板 + 多层投影 + 小三角）。
- **零素材依赖**：第二阶段的待机/互动动作全部用 `QPropertyAnimation` 变换动画
  （位移/缩放/旋转）实现，16 个内置造型与自定义图片/GIF 通用，不新增图片资源。
- **状态机统一调度**：第一阶段即建立 `PetStateMachine`，后续阶段只新增状态，不改动调度逻辑。
- **六语言文案**：所有用户可见文案走 `TranslationManager` 平铺键，六语言 JSON 同步补齐。
- **平台差异收敛**：第三阶段的系统状态采集按 `#ifdef Q_OS_MAC / Q_OS_WIN` 收敛到
  独立类中，业务层只面对统一接口。

## 三、总体架构

```
DesktopPetManager（单例，已有）
 ├── PetWindow          —— 宠物窗口（点击/右击/拖动/投喂，已有）
 ├── ClotheslineWindow  —— 晾衣绳历史（已有）
 ├── TodoWindow         —— 待办气泡（已有）
 ├── PetStateMachine    —— 宠物状态机（新增，第一阶段）
 │    状态：Idle 正常 / Remind 提醒（待办、番茄钟）/ Sleep 待机 / Interact 互动 / Alert 系统状态
 │    优先级：Interact > Alert > Remind > Sleep > Idle；高优先级打断低优先级，结束后回落
 ├── PetReminder        —— 提醒源（新增，第一阶段）：待办轮询、番茄钟计时
 └── PetSystemMonitor   —— 系统状态采集（新增，第三阶段，平台相关）
```

### 3.1 状态机（PetStateMachine）

| 状态 | 进入条件 | 表现 | 退出条件 |
|------|----------|------|----------|
| Idle | 无其他状态 | 正常造型 | 任一状态触发 |
| Remind | 待办逾期/今天到期、番茄钟到点 | 头顶红点气泡（待办）/ 提示卡（番茄钟） | 用户点开对应窗口、提醒被确认 |
| Sleep | N 分钟无互动（第二阶段） | 缓慢缩放呼吸 + 上下浮动 | 鼠标靠近/点击、高优先级状态 |
| Interact | 双击/拖动/投喂（第二阶段） | 弹跳/摇晃变换动画 | 动画结束 |
| Alert | CPU/内存超阈值、低电量（第三阶段） | 冒汗/瞌睡叠加图标 | 指标回落 |

## 四、阶段拆解

### 阶段一：提醒与效率（已完成）

#### 4.1.1 待办提醒红点

- `PetReminder` 内嵌 `QTimer`（30s 轮询 `TodoStore`），统计未完成且逾期/今天到期的条数。
- 有逾期 → 宠物头顶显示红色小圆点气泡（自绘圆 + 数量数字，复用气泡投影画法）；
  仅今天到期 → 橙色圆点；无 → 隐藏。
- 点击圆点气泡 → 展开待办气泡（复用 `DesktopPetManager::showTodoWindow`），
  关闭待办窗口后立即重估红点。
- 设置项：行为分组新增「待办提醒」开关（`pet/todoReminder`，默认开）。

#### 4.1.2 番茄钟/休息提醒

- `PetReminder` 增加番茄钟计时：工作 N 分钟 → 宠物跳跃 + 弹出提示卡（复用气泡自绘），
  卡片提供「休息 M 分钟」「跳过本次」两个按钮；休息结束再提示「继续工作」。
- 设置项：行为分组新增「番茄钟」开关（`pet/pomodoroEnabled`，默认关）、
  工作时长（`pet/pomodoroWorkMin`，默认 25）、休息时长（`pet/pomodoroBreakMin`，默认 5）。

### 阶段二：趣味陪伴（变换动画，无新素材）（已完成）

- `PetWindow` 增加 `playJump()` / `playWobble()` / `playBreathing()` 等变换动画方法，
  内部用 `QPropertyAnimation` 驱动 `pos`/`geometry` 或自定义 transform 属性。
- 待机检测：`PetStateMachine` 内 `QTimer` 检测距上次互动（点击/拖动/投喂）超过阈值
  （`pet/idleSleepMin`，默认 5）进入 Sleep；鼠标进入宠物区域退出 Sleep。
- 互动映射：双击 → 弹跳；拖动中 → 周期性轻摇；投喂到达 → 满足抖动（复用 chew 时机）。

### 阶段三：系统状态（已完成）

- 新增 `PetSystemMonitor`：5s 轮询；macOS 用 `host_statistics`（CPU 差分 /
  活跃+联动+压缩页内存占比）与 IOKit `IOPSCopyPowerSourcesInfo`（内置电池），
  Windows 用 `GetSystemTimes` / `GlobalMemoryStatusEx` / `GetSystemPowerStatus`。
- 阈值内置为常量：CPU 或内存 > 85% → 过载；电量 < 20% 且未充电 → 低电量
  （低电量优先于过载，且一次会话只提示一次）。设置页仅一个开关
  （`pet/sysMonitorEnabled`，默认关）。
- 视觉表现：`PetWindow` 左侧叠加角标（过载冒汗 / 低电量电池，96 基准坐标系自绘，
  内置造型与自定义素材通用），配色取 `PetTheme`（汗滴走强调色，电池描边走文字色、
  低量填充走逾期色，深浅色自适应）。

## 五、持久化键一览（ConfigManager/QSettings）

| 键 | 类型 | 默认 | 说明 |
|----|------|------|------|
| `pet/todoReminder` | bool | true | 待办红点提醒开关（阶段一） |
| `pet/pomodoroEnabled` | bool | false | 番茄钟开关（阶段一） |
| `pet/pomodoroWorkMin` | int | 25 | 番茄钟工作时长（分钟） |
| `pet/pomodoroBreakMin` | int | 5 | 番茄钟休息时长（分钟） |
| `pet/idleSleepEnabled` | bool | true | 待机打盹开关（阶段二） |
| `pet/idleSleepMin` | int | 5 | 无互动进入待机的分钟数（阶段二） |
| `pet/sysMonitorEnabled` | bool | false | 系统状态表情开关（阶段三） |

新增键同步登记到 `ConfigManager::scalarDefaults()`（注意 `pet/selection` 这类有迁移逻辑的键除外）。

## 六、UI 复用与新增

| 需求 | 方案 |
|------|------|
| 红点气泡 | 新增轻量 `PetBadgeWidget`，自绘圆形 + 数字，配色取 `PetTheme::overdueColor/todayColor` |
| 番茄钟提示卡 | 新增 `PomodoroCard`，复用 `TodoWindow` 的气泡自绘（圆角面板 + 投影 + 小三角）与 `PetTheme` 配色 |
| 设置项 | 全部进设置 → 桌面宠物 →「行为」分组，复用 `StyleManager::getSettingsCheckBoxStyle/getSettingsComboBoxStyle` 等 |
| 动画 | `QPropertyAnimation` + `QEasingCurve`，不引入第三方动画库 |

## 七、验收标准

- 阶段一：添加一条逾期待办 → 30s 内宠物头顶出现红点；点击红点展开待办；开启番茄钟到点后弹提示卡，休息/跳过流转正确；重启应用后开关与时长保持。
- 阶段二：5 分钟无互动进入呼吸浮动；双击弹跳、拖动摇晃、投喂抖动手感自然；任何状态下右击待办、点击晾衣绳不受影响。
- 阶段三：高负载时表情出现且 CPU 占用增量 < 1%；低电量提醒一次会话只提示一次。

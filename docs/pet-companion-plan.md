# 桌面宠物陪伴功能方案

## 一、需求概述

在现有桌面宠物（外观样式、投喂、晾衣绳、待办气泡）基础上，分四个阶段扩展陪伴能力：

1. **提醒与效率**：待办逾期/到期红点提醒；番茄钟/休息提醒。
2. **趣味陪伴**：待机动作（打瞌睡、张望）、互动反馈（摸头弹跳、拖动摇晃、投喂满足）。
3. **系统状态**：CPU/内存占用表情、低电量提醒。
4. **装饰与节日彩蛋**：宠物可戴装饰（小红花/草帽/墨镜/皇冠/圣诞帽/南瓜帽/派对帽），
   公历节日（圣诞/万圣/元旦）与生日当天自动戴应景帽子，次日自动消失。

> 范围说明：养成体系、互动增强、多宠物均不在本方案范围内。

## 二、设计原则

- **复用优先**：持久化统一走 `ConfigManager`（QSettings）；颜色与图标统一取 `PetTheme` /
  `StyleManager`；尺寸缩放统一走 `PetUi::scaled`；气泡/卡片复用 `TodoWindow` 的自绘方案
  （圆角面板 + 多层投影 + 小三角）。
- **零素材依赖**：第二阶段的待机/互动动作全部用 `QPropertyAnimation` 变换动画
  （位移/缩放/旋转）实现；第四阶段的装饰全部用 QPainter 在 96 基准坐标系自绘。
  两者都对 16 个内置造型与自定义图片/GIF 通用，不新增图片资源。
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

### 阶段四：装饰与节日彩蛋（已完成）

#### 4.4.1 装饰类型（Accessory 枚举）

| 值 | 枚举 | 造型要点 | 锚点 |
|----|------|----------|------|
| 0 | None | 无 | — |
| 1 | Flower | 小红花：五瓣圆 + 黄色花心 | 头顶偏侧 |
| 2 | StrawHat | 草帽：米黄帽檐 + 帽身 + 深色帽带 | 头顶 |
| 3 | Sunglasses | 墨镜：两片深色圆角镜片 + 鼻梁连接 | 眼部 |
| 4 | Crown | 皇冠：金色锯齿冠 + 顶端三颗珠子 | 头顶 |
| 5 | SantaHat | 圣诞帽：红色锥形 + 白色帽檐 + 白绒球 | 头顶 |
| 6 | PumpkinHat | 南瓜帽：橙色扁圆 + 绿色瓜柄 + 三角眼+咧嘴 | 头顶 |
| 7 | PartyHat | 派对帽：条纹圆锥 + 顶部绒球 | 头顶侧倾 |

- `PetWindow::setAccessory(Accessory)` / `accessory()`，切换后立即重绘。
- 绘制层放在 `paintEvent` 末尾（宠物本体 → 系统状态角标 → 装饰），帽子压在耳朵之上、
  墨镜覆盖眼睛；内置造型与自定义素材通用。
- 锚点统一取形体骨架的固定坐标（96 基准）：头顶 `(48,16)`、双眼 `(48,46)`。
  16 种内置造型共用同一骨架，无需按造型逐项配置；自定义素材同样在该基准坐标系内绘制。
- 配色取 `PetTheme`（节日红/橙走告警色系，金色/米色走暖色，深色镜片带透明度），
  描边用文字色保证深浅色主题下轮廓清晰。

#### 4.4.2 装饰来源与优先级

1. **节日自动**（最高优先，仅在开启节日彩蛋时生效）：
   - 圣诞 `12-24 ~ 12-25` → 圣诞帽
   - 万圣 `10-31 ~ 11-01` → 南瓜帽
   - 元旦 `12-31 ~ 01-01` → 派对帽
   - 生日（`pet/birthday` 为 `MM-dd`）当天 → 派对帽
   - 命中后覆盖当前装饰，节日区间结束自动回到手动装饰。
2. **手动选择**：设置页下拉，持久化 `pet/accessory`，默认 0（无）。

节日判定时机：启动时判定一次；随后用单次 `QTimer` 排到次日 0 点后 1s，
到点重新判定并重排（不做分钟级轮询）。节日开关关闭时停表并按手动装饰显示。

#### 4.4.3 设置项（设置 → 桌面宠物）

| 分组 | 控件 | 说明 |
|------|------|------|
| 外观 | 「装饰」下拉框 | 列出 8 种装饰，选中即生效（`pet/accessory`） |
| 行为 | 「节日彩蛋」复选框 | `pet/festivalEnabled`，默认开 |
| 行为 | 「生日」月下拉 + 日 SpinBox | 月下拉含「不设置」；未设置时禁用日 SpinBox；存 `pet/birthday` |

样式复用 `StyleManager::getSettingsComboBoxStyle()` / `getSettingsCheckBoxStyle()`，
并在 `refreshStyles()` 中重刷，保证深浅色主题切换后一致；QSS 需显式给出 `:disabled`
文字色，否则覆写 `color` 后禁用控件看起来仍可编辑。生日用下拉 + SpinBox（不引入
QDateEdit，避免新控件的深浅色适配工作），日上限随月份收敛（如 2 月最多 29 天）。

#### 4.4.4 自测结论

- 8 种装饰在浅色 / 深色主题下逐个离屏截图确认：轮廓与配色清晰，帽子贴合头顶，
  墨镜准确覆盖双眼。
- 设置页浅/深色截图确认装饰行、节日彩蛋、生日行排布正常。
- 六语言 JSON 键数一致（各 449 个，本阶段新增 12 个键）。

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
| `pet/accessory` | int | 0 | 手动装饰（Accessory 枚举值，阶段四） |
| `pet/festivalEnabled` | bool | true | 节日彩蛋开关（阶段四） |
| `pet/birthday` | string | "" | 宠物生日 `MM-dd`，空为未设置（阶段四） |

新增键同步登记到 `ConfigManager::scalarDefaults()`（注意 `pet/selection` 这类有迁移逻辑的键除外）。

## 六、UI 复用与新增

| 需求 | 方案 |
|------|------|
| 红点气泡 | 新增轻量 `PetBadgeWidget`，自绘圆形 + 数字，配色取 `PetTheme::overdueColor/todayColor` |
| 番茄钟提示卡 | 新增 `PomodoroCard`，复用 `TodoWindow` 的气泡自绘（圆角面板 + 投影 + 小三角）与 `PetTheme` 配色 |
| 设置项 | 全部进设置 → 桌面宠物 →「行为」分组，复用 `StyleManager::getSettingsCheckBoxStyle/getSettingsComboBoxStyle` 等 |
| 动画 | `QPropertyAnimation` + `QEasingCurve`，不引入第三方动画库 |
| 装饰 | 不新增控件/素材，统一在 `PetWindow::paintEvent` 末尾按 96 基准坐标用 QPainter 自绘叠加 |

## 七、验收标准

- 阶段一：添加一条逾期待办 → 30s 内宠物头顶出现红点；点击红点展开待办；开启番茄钟到点后弹提示卡，休息/跳过流转正确；重启应用后开关与时长保持。
- 阶段二：5 分钟无互动进入呼吸浮动；双击弹跳、拖动摇晃、投喂抖动手感自然；任何状态下右击待办、点击晾衣绳不受影响。
- 阶段三：高负载时表情出现且 CPU 占用增量 < 1%；低电量提醒一次会话只提示一次。
- 阶段四：设置页选择「草帽」→ 宠物头顶立即出现草帽，切回「无」→ 消失；
  系统日期改到 12-25 重启 → 自动出现圣诞帽，改回普通日期 → 恢复手动装饰；
  生日设为今天 → 出现派对帽，设为其他日期 → 不出现；关闭「节日彩蛋」→ 节日日期也不自动装饰；
  16 种内置造型与自定义素材模式下装饰位置一致；浅色/深色主题轮廓清晰；六语言 JSON 键数一致。

# 桌面宠物定时推送方案（定时打开网页）

> 状态：已实施（阶段一 `505f4e1`、阶段二 `30a0e07`、阶段三随「吐出网页动效」提交）。实现与方案的偏差在对应小节标注。

## 一、需求概述

在桌面宠物设置的行为分组中新增「定时推送」，支持配置多条定时任务：

- 每条任务可设置：启用开关、生效的星期（周一至周日，可多选）、触发时间（时:分）、网址。
- 到达设定时间时，宠物播放「吐出网页」动效：张嘴 → 一张网页卡片从宠物嘴边飞出、放大、淡出，
  随后通过系统默认浏览器打开对应网址。
- 仅宠物开启期间执行；休眠、程序未运行等错过的时间点不补执行。

## 二、设计原则

- **复用优先**：存储沿用 `TodoStore` 的独立 JSON 文件模式（应用数据目录，`ConfigManager`
  只管扁平标量配置）；宠物动作复用 `PetWindow::openMouth()`；飞行卡片复用
  `FlyAnimationWidget` 的透明置顶窗口方案；尺寸统一走 `PetUi::scaled`。
- **职责单一**：任务数据、时间匹配、动画播放、浏览器打开四件事分开，时间匹配逻辑
  独立成可单测的纯函数。
- **多语言**：全部文案走 `TranslationManager` 平铺键，五语言 JSON 同步补齐。
- **不引入网络依赖**：网页卡片按域名文字自绘，不请求 favicon。

## 三、数据模型与存储

新增 `ScheduledTask`（`src/pet/ScheduledTaskStore.h`）：

| 字段 | 类型 | 说明 |
|------|------|------|
| id | QString | UUID |
| enabled | bool | 是否启用 |
| days | int | 星期位掩码，bit0=周一 … bit6=周日（周一~周五 = 62） |
| time | QString | `HH:mm`（24 小时制） |
| url | QString | 目标网址，保存时规范化（无协议补 `https://`） |

存储：应用数据目录下 `scheduled_tasks.json`，格式 `{"version":1,"tasks":[...]}`，
每次写操作立即落盘；文件缺失或损坏时返回空列表，不阻塞启动。

`ScheduledTaskStore`（单例）提供 `list / add / update / remove`，变更后发出
`changed()` 信号。

## 四、时间匹配与调度

新增 `ScheduledPushRunner`（`src/pet/ScheduledPushRunner.h`）：

- `QTimer` 15s 周期 tick（与 `PetReminder` 的轮询风格一致），由
  `DesktopPetManager` 在宠物开启时 `start()`、关闭时 `stop()`。
- 每次 tick 判定：`enabled`、`days` 包含今天、`time` 等于当前 `HH:mm`。
- 去重键 `taskId + "yyyy-MM-dd HH:mm"`，同一任务同一分钟只触发一次；
  不落盘，仅存内存（进程重启后不补执行，符合「错过不补」）。
- 多条任务同分钟命中时按列表顺序排队，每条间隔 1.2s 依次播放。
- `ScheduledTaskStore::changed()` → `runner.reload()`，设置页修改即时生效。

触发动作：`DesktopPetManager` 收到 `taskDue(task)` 后播放吐出动效，
动效结束（或宠物未显示时直接）调用 `QDesktopServices::openUrl()`。

## 五、吐出动效

1. `PetWindow` 新增 `playSpitPose()`：身体后仰蓄力（`chewScale` 1.0 → 0.88，100ms），
   随后 `openMouth()` 张嘴；整体约 380ms 后自行闭嘴。
2. 新增 `WebFlyOutWidget`（`src/pet/WebFlyOutWidget.h`）：
   - 无边框、透明背景、置顶、鼠标穿透的独立窗口；
   - 起点为 `PetWindow::mouthCenterGlobal()`，沿二次曲线飞向宠物上方偏屏幕中央的位置，
     尺寸约 40 → 220px，轻微旋转，末段淡出，总时长约 700ms；
   - 卡片自绘：白色圆角底 + 顶部三个圆点（浏览器窗口意象）+ 地球图标 + 域名文字；
   - 结束发出 `finished()`，随后打开浏览器。
3. 动效与打开浏览器的衔接：`finished` 与 `openUrl` 之间不留可见空档（卡片淡出到 30%
   时即打开浏览器）。

## 六、设置界面

桌面宠物选项卡「行为」分组底部新增「定时推送」区块：

- 说明文字一行（到点宠物会吐出网页并用默认浏览器打开）。
- 任务卡片列表（每个任务一块，随选项卡滚动区滚动）：
  - 第一行：启用复选框、`QTimeEdit`（HH:mm）、周一至周日七个可勾选小按钮、删除按钮；
  - 第二行：「网址」标签 + `QLineEdit`（占位符 `https://example.com`）。
- 底部「＋ 添加任务」按钮；无任务时显示空状态提示。
- 控件样式复用 `StyleManager::getSettingsCheckBoxStyle / getSettingsComboBoxStyle`
  与设置页按钮规范；`QTimeEdit` 使用与 SpinBox 一致的输入框样式。

## 七、多语言

`src/languages/{zh_CN,en_US,zh_TW,ja_JP,ko_KR}.json` 新增：

`pet.scheduledPush`、`pet.scheduledPush.hint`、`pet.scheduledPush.url`、
`pet.scheduledPush.urlPlaceholder`、`pet.scheduledPush.add`、
`pet.scheduledPush.empty`、`pet.scheduledPush.delete`、`pet.scheduledPush.days.*` 等。

## 八、阶段拆解

### 阶段一：数据与调度（不含界面与动效）

- `ScheduledTask` / `ScheduledTaskStore` / `ScheduledPushRunner`；
- `DesktopPetManager` 接线：宠物开启时启动、关闭时停止，任务到点直接打开浏览器；
- 单元测试：星期位掩码匹配、时间匹配、同分钟去重、JSON 读写往返。

### 阶段二：设置界面与多语言

- 行为分组新增定时推送区块与任务卡片；
- 增删改、启停即时持久化并对运行中的调度器生效；
- 五语言文案补齐。

### 阶段三：吐出动效（已完成）

- `PetWindow::playSpitPose()`；
- `WebFlyOutWidget` 飞出动画与打开浏览器衔接；
- 文档状态更新。

> **实现说明**：`playSpitPose()` 复用 `chewScale`/`mouthOpen` 属性做「下压蓄力 → 张嘴 → 闭嘴」，
> 与投喂/咀嚼共用 `m_mouthAnim`，互不覆写。卡片在张嘴后 320ms 起飞，沿「嘴部 → 屏幕中心」
> 方向飞出 300px 并放大到 220px，进度 0.62 起淡出，同时触发 `opening()` 打开浏览器。
> 宠物在张嘴期间被关闭时放弃本次推送。

## 九、边界与风险

- **URL 校验**：保存时用 `QUrl::fromUserInput` 规范化；无法解析为有效 http/https 地址时
  保留用户输入但不触发打开，并在设置页输入框标红提示。
- **宠物未开启**：调度器不启动，不触发。
- **系统时间回拨/休眠唤醒**：tick 按「当前分钟精确匹配 + 去重键」判定，
  不计算时间差，因此休眠期间错过的时间点不会补触发。
- **同一分钟多条任务**：排队依次播放，避免多个卡片动画叠在一起。
- **任务数量**：不设硬上限，但设置页滚动区承载，建议提示用户控制在合理数量。

## 十、测试计划

- `tests/ScheduledPushTest.cpp`：
  - 星期位掩码：单日、工作日、全周、空掩码；
  - 时间格式解析与非法值；
  - 同一分钟重复 tick 只触发一次；
  - `ScheduledTaskStore` 增删改查与 JSON 往返；
  - 清除 HTTP/HTTPS 前缀规范化结果。

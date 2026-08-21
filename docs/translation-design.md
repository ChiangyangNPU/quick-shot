# QuickShot 翻译功能技术文档

## 1. 概述

QuickShot 的翻译功能基于截图 OCR 识别结果，将识别出的文字翻译为目标语言。
提供两种使用形式：

| 形式 | 说明 | 使用场景 |
|---|---|---|
| OCR 结果弹窗翻译 | 在 OCR 识别结果弹窗底部点击「翻译」，弹窗内切换原文/译文/对照 | 查看 OCR 结果时顺带翻译 |
| 译文叠加显示 | 在截图工具栏 / 录屏工具栏 / 钉图窗口右键点击「翻译」，弹出独立窗口将译文叠加到原图位置 | 对比阅读，保留版式 |

### 1.1 技术栈

| 组件 | 技术 | 说明 |
|------|------|------|
| 翻译引擎抽象 | 纯 Qt QObject 子类 | 统一异步 translate() 接口与错误码体系 |
| 默认引擎 | MyMemory 免费 API（免注册） | 国内可直连，带 email 提升额度至 50000 词/天 |
| 扩展引擎 | 百度翻译 / DeepL / LibreTranslate | 用户自填 Key / URL，软件不预置任何凭证 |
| 网络请求 | Qt6::Network QNetworkAccessManager | HTTP GET + JSON 解析 |
| TLS 支持 | Qt TLS 后端插件 post-build 部署 | 确保 HTTPS 请求正常 |
| 批量翻译 | 逐段顺序 + 失败回退原文 | 避免单段失败中断整体流程 |
| 国际化 | 项目 TranslationManager JSON 翻译键 | 6 种语言：zh_CN/en_US/ja_JP/ko_KR/zh_TW/zh_HK |

### 1.2 核心特性

- **默认开箱即用**：MyMemory 引擎免注册，首次使用弹出隐私提示
- **多引擎可切换**：设置页提供百度/DeepL/LibreTranslate 自填 Key 入口
- **弹窗内三态对照**：OcrResultDialog 内切换原文/译文/对照
- **译文位置叠加**：译文按 OCR polygons 位置叠加回原图
- **独立叠加窗口**：TranslateOverlayWindow 支持三态切换、滚轮缩放、拖动、另存、复制原文/译文
- **文字选择模式**：叠加窗口内勾选「选择文字」，可自由跨段拖选复制
- **错误码本地化**：8 种错误分类 → 查翻译键显示，「详细信息」保留原始报错
- **公共方法复用**：TranslateService 统一前置检查 + 错误消息；TranslateOverlayWindow::translateAndShow 封装批量翻译全流程

---

## 2. 整体架构

### 2.1 架构流程图

```mermaid
flowchart LR
    U["用户操作"] --> A
    subgraph 入口层
        A["OcrResultDialog 翻译按钮"]
        B["截图工具栏翻译"]
        C["录屏工具栏翻译"]
        D["钉图窗口右键翻译"]
    end

    subgraph 翻译服务层
        E["TranslateService 单例"]
        E1["checkEnabledAndPrivacy"]
        E2["errorMessage"]
        E3["translateBatch 状态机"]
    end

    subgraph OCR 层
        O["OcrEngine"]
    end

    subgraph 引擎实现层
        F["MyMemoryEngine 默认"]
        G["BaiduTranslateEngine"]
        H["DeepLEngine"]
        I["LibreTranslateEngine"]
    end

    subgraph 结果展示层
        J["OcrResultDialog 三态视图"]
        K["TranslateOverlayWindow"]
        K1["QPainter 绘制译文"]
        K2["文字选择 QTextEdit"]
    end

    subgraph 配置层
        L["ConfigManager translate 配置"]
        M["SettingsWindow 翻译选项卡"]
    end

    A --> E
    B --> O --> E
    C --> O --> E
    D --> O --> E
    E --> E1
    E --> E2
    E --> E3
    E3 --> F
    E3 --> G
    E3 --> H
    E3 --> I
    E -->|finished| J
    E -->|batchFinished| K
    K --> K1
    K -->|"选择文字模式"| K2
    L --> E
    M --> L
```

### 2.2 分层说明

- **入口层**：覆盖所有触发点
- **翻译服务层**：`TranslateService` 单例，统一管理引擎实例、配置加载、批量翻译状态机、公共静态方法
- **引擎实现层**：抽象 `TranslateEngine` 接口 + 4 种具体实现
- **结果展示层**：`OcrResultDialog`（三态视图）与 `TranslateOverlayWindow`（译文叠加 + 文字选择）
- **配置层**：`ConfigManager` 管理配置；`SettingsWindow` 提供 UI

---

## 3. 核心类图

```mermaid
classDiagram
    class QObject

    class TranslateError {
        <<enumeration>>
        NetworkFailed
        SslFailed
        SameLanguage
        RateLimit
        NotConfigured
        ApiError
        EmptyText
        Unknown
    }

    class TranslateEngine {
        <<abstract>>
        +name() QString*
        +isAvailable() bool*
        +requiresApiKey() bool*
        +translate(text, srcLang, tgtLang) void*
        +finished(original, translated) signal
        +failed(code, detail) signal
    }

    class MyMemoryEngine {
        -m_email QString
        +setEmail(email) void
        +name() QString
        +isAvailable() bool
        +requiresApiKey() bool
        +translate(text, srcLang, tgtLang) void
    }

    class BaiduTranslateEngine {
        -m_appId QString
        -m_key QString
        +setAppId(id) void
        +setKey(key) void
        +name() QString
        +isAvailable() bool
        +requiresApiKey() bool
        +translate(text, srcLang, tgtLang) void
        -sign(query, salt) QString
    }

    class DeepLEngine {
        -m_key QString
        +setKey(key) void
        +name() QString
        +isAvailable() bool
        +requiresApiKey() bool
        +translate(text, srcLang, tgtLang) void
    }

    class LibreTranslateEngine {
        -m_url QString
        +setUrl(url) void
        +name() QString
        +isAvailable() bool
        +requiresApiKey() bool
        +translate(text, srcLang, tgtLang) void
    }

    class TranslateService {
        <<singleton>>
        -m_engines QMap<QString, TranslateEngine*>
        -m_currentEngine TranslateEngine*
        -m_batchOriginals QStringList
        -m_batchTranslated QStringList
        -m_batchIndex int
        -m_batchTotal int
        -m_batchActive bool
        +instance() TranslateService$
        +loadConfig(settings) void
        +setCurrentEngine(name) void
        +translate(text, srcLang, tgtLang) void
        +translateBatch(texts, srcLang, tgtLang) void
        +checkEnabledAndPrivacy(parent, centerRect) bool$
        +errorMessage(code) QString$
        +finished(original, translated) signal
        +failed(code, detail) signal
        +batchFinished(translatedTexts) signal
        +onEngineFinished(original, translated) slot
        +onEngineFailed(code, detail) slot
        +translateNextBatchItem() slot
    }

    class TranslateOverlayWindow {
        -m_pixmap QPixmap
        -m_texts QStringList
        -m_polygons QVector<QPolygonF>
        -m_translatedTexts QStringList
        -m_viewMode ViewMode
        -m_textSelectionMode bool
        -m_selectEditor QTextEdit*
        +setViewMode(mode) void
        +setTextSelectionMode(enabled) void
        +translateAndShow(parent, pixmap, texts, polygons, overlayPos, labelRect, onOverlayShown) void$
        +eventFilter(watched, event) bool
        +copyOriginal() void
        +copyTranslation() void
        +saveAsImage() void
        +drawOverlay(painter) void
        +drawTranslatedText(painter, text, rect) void
    }

    class OcrResultDialog {
        -m_fullText QString
        -m_translatedText QString
        -m_viewMode int
        +onTranslate() slot
        +switchViewMode(mode) slot
    }

    class PinWindow {
        +performTranslate() slot
    }

    class SnipScreen {
        +performTranslate() slot
    }

    QObject <|-- TranslateEngine
    QObject <|-- TranslateService
    TranslateEngine <|-- MyMemoryEngine
    TranslateEngine <|-- BaiduTranslateEngine
    TranslateEngine <|-- DeepLEngine
    TranslateEngine <|-- LibreTranslateEngine

    TranslateService --> "1..n" TranslateEngine : 管理
    TranslateService --> TranslateError : 错误分类
    TranslateService --> OcrResultDialog : finished
    TranslateService --> TranslateOverlayWindow : batchFinished

    SnipScreen --> TranslateOverlayWindow : translateAndShow
    PinWindow --> TranslateOverlayWindow : translateAndShow
    SnipScreen --> TranslateService : checkEnabledAndPrivacy
    PinWindow --> TranslateService : checkEnabledAndPrivacy
    OcrResultDialog --> TranslateService : checkEnabledAndPrivacy, translate
```

---

## 4. 翻译引擎层设计

### 4.1 抽象接口 TranslateEngine

文件：[TranslateEngine.h](file:///e:/develop/Code/github_new/quick-shot/src/translate/TranslateEngine.h)

错误码枚举：

```cpp
enum class TranslateError {
    NetworkFailed,   ///< 网络错误（DNS/超时/断开）
    SslFailed,       ///< SSL/TLS 未配置或握手失败
    SameLanguage,    ///< 源语言与目标语言相同
    RateLimit,       ///< 翻译额度用尽
    NotConfigured,   ///< 引擎未配置（缺 Key/URL）
    ApiError,        ///< 引擎业务错误（非 200）
    EmptyText,       ///< 空文本
    Unknown          ///< 未知错误兜底
};
```

### 4.2 四种引擎对比

| 引擎 | 类型 | 配置项 | 国内可达 | 额度 | 说明 |
|---|---|---|---|---|---|
| MyMemoryEngine | 默认，免注册 | 可选 email | ✅ 直连 | 无 email:5000 词/天<br>有 email:50000 词/天 | 签名不加密，最容易使用 |
| BaiduTranslateEngine | Key-Based | appId + key | ✅ 国内服务 | 每月免费 200 万字符 | 签名：`MD5(appId+query+salt+key)` |
| DeepLEngine | Key-Based | key | ❌ 需联网 | 按 Key 付费 | 翻译质量最高 |
| LibreTranslateEngine | URL-Based | 服务地址 | ✅ 自托管可达 | 取决于自托管服务 | 完全离线可实现 |

所有 Key / URL 由用户在设置页填写，存储于本地 QSettings，**软件不预置任何凭证**。

---

## 5. 配置项设计

统一前缀 `translate/`，通过 `ConfigManager::getSettings()` 读写。

| 配置键 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `translate/enabled` | bool | true | 未启用时所有翻译入口不响应 |
| `translate/engine` | QString | "mymemory" | 当前引擎 |
| `translate/targetLang` | QString | "en" | 目标语言 |
| `translate/sourceLang` | QString | "auto" | 源语言 |
| `translate/mymemoryEmail` | QString | "" | MyMemory 邮箱（提升额度） |
| `translate/baiduAppId` | QString | "" | 百度 App ID |
| `translate/baiduKey` | QString | "" | 百度 Key |
| `translate/deeplKey` | QString | "" | DeepL Key |
| `translate/libreUrl` | QString | "" | LibreTranslate 地址 |
| `translate/showPrivacyWarning` | bool | true | 首次隐私提示开关 |

### 5.1 SettingsWindow 翻译选项卡

位置在「样式」与「历史记录」之间：**常规 / 快捷键 / 样式 / 翻译 / 历史记录 / 关于**。

```mermaid
flowchart TD
    subgraph 翻译选项卡
        A["翻译引擎下拉框"] --> A1["MyMemory 免注册"]
        A --> A2["百度翻译"]
        A --> A3["DeepL"]
        A --> A4["LibreTranslate 自托管"]
        B["翻译语言下拉框"]
        C["高级配置区"]
        C1["MyMemory Email"]
        C2["百度 AppId + Key"]
        C3["DeepL Key"]
        C4["LibreTranslate URL"]
        D["启用翻译功能 checkbox"]
        E["首次隐私提示 checkbox"]
    end
    A -->|"选中显示对应配置"| C
```

> 约定：翻译选项卡内容不多，属于「非滚动 tab」，在 `onTabChanged` 中用 `setFixedHeight` 强制高度自适应内容。

---

## 6. 形式一：OCR 结果弹窗内翻译

### 6.1 流程图

```mermaid
flowchart TD
    Start(["用户打开 OcrResultDialog"]) --> Btn(["点击翻译按钮"])
    Btn --> Check{"TranslateService checkEnabledAndPrivacy"}
    Check -->|"未启用"| End1(["不处理"])
    Check -->|"首次使用"| Dlg["隐私提示弹窗"]
    Dlg -->|"用户拒绝"| End2(["结束"])
    Dlg -->|"用户同意"| S1["translate 整段翻译"]
    Check -->|"已启用且已同意"| S1

    S1 --> S2["MyMemoryEngine HTTP GET"]
    S2 -->|"成功"| Ok["译文写入 m_translatedText 默认切换到译文视图"]
    S2 -->|"失败"| Err["弹窗显示 errorMessage 加详细信息按钮查看 detail"]

    Ok --> Switch["用户切换视图"]
    Switch --> V1["原文"]
    Switch --> V2["译文"]
    Switch --> V3["上下对照"]
```

### 6.2 时序图

```mermaid
sequenceDiagram
    actor U as 用户
    participant D as OcrResultDialog
    participant S as TranslateService
    participant E as MyMemoryEngine
    participant N as 网络请求

    U->>D: 点击翻译按钮
    D->>D: checkEnabledAndPrivacy(this)
    D->>S: checkEnabledAndPrivacy
    alt 首次使用且 showPrivacyWarning=true
        S-->>D: 弹隐私提示（带不再提示复选框）
        U->>D: 同意
        S->>S: 若勾选不再提示则置 showPrivacyWarning=false
    end
    S-->>D: 返回 true=可以翻译

    D->>S: translate(m_fullText)
    S->>E: translate(text, auto, en)
    E->>N: GET api.mymemory.translated.net/get
    N-->>E: JSON 返回
    alt 成功
        E-->>S: finished(original, translated)
        S-->>D: finished(original, translated)
        D->>D: 保存 translatedText 切换视图到译文
        D-->>U: 显示译文
    else 失败（网络/额度/API错误等）
        E-->>S: failed(code, detail)
        S-->>D: failed(code, detail)
        D->>D: errorMessage(code) 查本地化文案
        D-->>U: 失败弹窗（确定 + 详细信息按钮）
        U->>D: 点击详细信息
        D-->>U: 次级对话框显示原始 detail
    end
```

### 6.3 三态视图切换逻辑

```mermaid
stateDiagram-v2
    [*] --> 原文 : OCR完成后默认
    原文 --> 译文 : 翻译成功后自动切换
    译文 --> 原文 : 点击原文标签
    译文 --> 对照 : 点击对照标签
    原文 --> 对照 : 点击对照标签（有译文时）
    对照 --> 原文 : 点击原文标签
    对照 --> 译文 : 点击译文标签
```

- **原文视图**：纯 OCR 识别文本
- **译文视图**：纯翻译后文本
- **对照视图**：按段原文 + 译文上下间隔显示（或左右对照）

### 6.4 翻译粒度

- 整段翻译：把 `m_fullText`（OCR 结果按行合并）整体送译
- 优点：请求次数少、额度消耗低、实现简单

---

## 7. 形式二：译文叠加显示

### 7.1 三处入口

| 入口 | 触发方式 | OCR 时机 |
|---|---|---|
| 截图工具栏 | ScreenshotToolBar 点击翻译按钮 | 截取当前选区图像 → OCR |
| 录屏工具栏 | RecordingToolBar 点击翻译按钮 | 截取当前选区图像 → OCR |
| 钉图窗口右键 | PinWindow 右键菜单「翻译」（复制→OCR→翻译→保存） | 对钉图 m_pixmap → OCR |

### 7.2 完整流程图

```mermaid
flowchart TD
    Start(["点击翻译"]) --> GetPix["截取选区图像 / 取钉图图像"]
    GetPix --> Check{"checkEnabledAndPrivacy parent centerRect"}
    Check -->|"未启用"| End0(["不处理"])
    Check -->|"首次使用"| PrivacyDlg["隐私提示弹窗 centerRect 选区坐标居中"]
    PrivacyDlg -->|"拒绝"| End1(["结束"])
    PrivacyDlg -->|"同意"| OcrStart["异步OCR开始 显示识别中标签居中选区"]

    Check -->|"已启用且已同意"| OcrStart

    OcrStart --> OcrWait["..."]
    OcrWait --> OcrEmpty{"结果为空?"}
    OcrEmpty -->|"是"| EmptyTip["显示未识别到文字标签 3秒自动消失"] --> End2(["结束"])
    OcrEmpty -->|"否"| Translate["TranslateOverlayWindow translateAndShow 一站式"]

    subgraph translateAndShow
        T1["创建翻译中加载标签 居中 labelRect"]
        T2["连接 batchFinished 信号"]
        T3["连接 failed 信号"]
        T4["发起 translateBatch"]
    end
    Translate --> T1
    T1 --> T2
    T1 --> T3
    T2 --> T4
    T3 --> T4

    T4 -->|"batchFinished"| Ok["关闭加载标签 创建 TranslateOverlayWindow 定位到 overlayPos 并 show"]
    T4 -->|"failed"| Fail["关闭加载标签 显示 errorMessage 标签 3秒自动消失"]

    Ok --> UserOper["用户操作"]
    Fail --> End3(["结束"])

    subgraph 用户操作
        U1(["切换视图"])
        U2(["勾选选择文字 鼠标拖选 Ctrl+C"])
        U3(["右键菜单 复制原文或译文"])
        U4(["右键 另存为图片"])
        U5(["拖动 滚轮缩放"])
        U6(["ESC 先退出选择模式 再按关闭窗口"])
    end
    UserOper --> U1
    UserOper --> U2
    UserOper --> U3
    UserOper --> U4
    UserOper --> U5
    UserOper --> U6
```

### 7.3 时序图（以 SnipScreen 截图工具栏翻译为例）

```mermaid
sequenceDiagram
    actor U as 用户
    participant S as SnipScreen
    participant T as ScreenshotToolBar
    participant O as OcrEngine
    participant TS as TranslateService
    participant OW as TranslateOverlayWindow
    participant EN as TranslateEngine

    U->>T: 点击翻译按钮
    T-->>S: emit translateRequested()
    S->>S: 截取当前选区图像 QPixmap
    S->>TS: checkEnabledAndPrivacy(this, selRect)
    alt 首次使用
        TS-->>S: 隐私提示弹窗（居中 selRect）
        U->>S: 同意
    end
    TS-->>S: true

    S->>S: 显示「识别中...」标签居中选区
    S->>O: recognize(image) 异步
    O-->>S: OcrResult(texts, polygons)
    alt texts 为空
        S->>S: 显示「未识别到文字」3秒临时标签
    else texts 非空
        S->>OW: translateAndShow(this, pixmap, texts, polygons, selPos, labelRect, onOverlayShown=exit)
        Note over OW: 创建翻译中标签 连接 batchFinished 和 failed（一次性）
        OW->>TS: translateBatch(texts)

        loop 逐段顺序翻译
            TS->>EN: translate(segment_i)
            EN-->>TS: finished 或 failed(回退原文)
            TS->>TS: 下一段
        end

        alt 全部段完成
            TS-->>OW: batchFinished(译文列表)
            OW->>OW: 关闭翻译中标签
            OW->>OW: new TranslateOverlayWindow 定位到 selPos
            OW-->>U: 显示叠加窗口
            OW->>S: onOverlayShown 回调
            S->>S: exit 退出截图框和工具栏
        else 引擎未配置等全局失败
            TS-->>OW: failed(code, detail)
            OW->>OW: 关闭翻译中标签
            OW->>OW: 显示 errorMessage 临时标签3秒
        end
    end

    Note over U,OW: === 叠加窗口后续交互 ===
    U->>OW: 右键菜单 → 勾选文字选择模式
    OW->>OW: setTextSelectionMode(true) 创建全屏 QTextEdit 包含全部译文
    U->>OW: 鼠标跨段拖选 + Ctrl+C
    OW-->>U: 复制到剪贴板
    U->>OW: 选字模式内右键
    OW->>OW: eventFilter 拦截 Copy 全选 本地化 + 追加文字选择模式 + 取消
    U->>OW: 取消勾选文字选择模式
    OW->>OW: setTextSelectionMode(false) 回到 Overlay 绘制
    U->>OW: ESC
    OW->>OW: 先退出选择模式
    U->>OW: 再按 ESC
    OW-->>U: 关闭窗口
```

### 7.4 批量翻译状态机

```mermaid
stateDiagram-v2
    [*] --> Idle
    Idle --> Active : translateBatch 非空 texts
    Active --> TranslatingCurrent : translateNextBatchItem
    TranslatingCurrent --> IndexPlus : 当前段 finished
    TranslatingCurrent --> IndexPlus : 当前段 failed 回退原文占位 LOG_WARNING
    IndexPlus -->|"idx 小于 total"| TranslatingCurrent
    IndexPlus -->|"idx 等于 total"| Finished
    Finished --> Idle : emit batchFinished
```

状态机成员（`TranslateService`）：
```cpp
QStringList m_batchOriginals;   // 原文列表
QStringList m_batchTranslated;  // 译文列表
int m_batchIndex = 0;           // 当前索引
int m_batchTotal = 0;           // 总段数
bool m_batchActive = false;     // 是否在批量中
```

### 7.5 TranslateOverlayWindow 渲染与文字选择

```mermaid
flowchart TB
    subgraph 渲染模式判定
        R["paintEvent 入口"] --> C1{"viewMode 等于 Original?"}
        C1 -->|"是"| R1["仅画原图"]
        C1 -->|"否"| C2{"textSelectionMode?"}
        C2 -->|"是"| R1
        C2 -->|"否"| R2["画原图 加 drawOverlay 每个多边形画半透明背景 加自动换行缩放字号的译文"]
    end

    subgraph 文字选择模式
        S0["setTextSelectionMode true"] --> S1["new QTextEdit 覆盖全屏 ReadOnly 加 TextSelectableByMouse"]
        S1 --> S1b["viewport installEventFilter 拦截右键"]
        S1b --> S2["setPlainText translatedTexts 按段拼接"]
        S2 --> S3["半透明白背景 加 深色文字"]
        S3 --> S4["用户交互"]
        S4 --> S5{"ESC?"}
        S5 -->|"是"| S6["setTextSelectionMode false deleteLater QTextEdit"]
        S4 --> S7{"右键?"}
        S7 -->|"是"| S9["eventFilter 拦截 viewport ContextMenu"]
        S9 --> S10["createStandardContextMenu 保留 Copy 全选 本地化替换"]
        S10 --> S11["追加 文字选择模式勾选 加 取消 两项"]
        S11 --> S12{"用户选择"}
        S12 -->|"取消勾选文字选择模式"| S6
        S12 -->|"取消"| S13["setTextSelectionMode false 加 close Overlay"]
        S12 -->|"Copy 或 全选"| S8["QTextEdit 原生处理 到 剪贴板"]
        S4 --> S14{"Ctrl+C"}
        S14 -->|"是"| S8
    end

    subgraph 视图切换
        V1["setViewMode"] --> V2{"textSelectionMode?"}
        V2 -->|"是"| V3["重新创建 QTextEdit 更新 geometry"]
        V2 -->|"否"| V4["update 触发 drawOverlay 重绘"]
    end
```

---

## 8. 隐私与错误处理

### 8.1 首次隐私提示定位策略

```mermaid
flowchart TD
    Start["checkEnabledAndPrivacy parent centerRect 入口"] --> ShowWarning{"showPrivacyWarning?"}
    ShowWarning -->|"false"| RetTrue(["返回 true"])
    ShowWarning -->|"true"| CreateDlg["创建 QMessageBox"]

    CreateDlg --> PosCalc{"centerRect isValid?"}
    PosCalc -->|"是 SnipScreen 传入选区"| Calc1["弹窗中心等于 centerRect 中心"]
    PosCalc -->|"否 PinWindow 或 Dialog"| Calc2["弹窗中心等于 parent frameGeometry 中心"]
    Calc1 --> MoveTimer["QTimer singleShot 0 msgBox move"]
    Calc2 --> MoveTimer

    MoveTimer --> Exec["msgBox exec"]
    Exec --> UserClick{"点击的按钮"}
    UserClick -->|"YES 加 勾选不再提示"| SaveCfg["translate showPrivacyWarning 置 false"] --> RetTrue
    UserClick -->|"YES"| RetTrue
    UserClick -->|"NO"| RetFalse(["返回 false"])
```

### 8.2 错误码 → 本地化消息对应表

| TranslateError | 翻译键 | zh_CN 示例 |
|---|---|---|
| NetworkFailed | `translate.error.networkFailed` | 网络连接失败，请检查网络 |
| SslFailed | `translate.error.sslFailed` | SSL/TLS 配置错误，无法建立安全连接 |
| SameLanguage | `translate.error.sameLanguage` | 源语言与目标语言相同 |
| RateLimit | `translate.error.rateLimit` | 翻译额度用尽，请稍后重试或更换引擎 |
| NotConfigured | `translate.error.notConfigured` | 翻译引擎未配置，请在设置中完善配置 |
| ApiError | `translate.error.apiError` | 翻译服务返回错误，请稍后重试 |
| EmptyText | `translate.error.emptyText` | 翻译文本为空 |
| Unknown | `translate.error.unknown` | 未知错误 |

失败弹窗结构：
```mermaid
flowchart LR
    A["失败弹窗"] --> B["主文案 等于 errorMessage(code) 本地化结果"]
    A --> C["按钮1 确定 tm get ok"]
    A --> D["按钮2 详细信息 tm get translate showDetails"]
    D -->|"点击"| E["次级对话框"]
    E --> F["显示原始 detail 英文原文 不翻译 保留调试信息"]
```

---

## 9. 公共复用方法（重构提取）

| 方法签名 | 定义位置 | 消除的重复点 |
|---|---|---|
| `TranslateService::checkEnabledAndPrivacy(parent, centerRect) → bool` | TranslateService 静态 | OcrResultDialog、SnipScreen、PinWindow 3 处各自实现的启用检查 + 隐私提示弹窗 |
| `TranslateService::errorMessage(code) → QString` | TranslateService 静态 | 3 处入口各自实现的 TranslateError → 本地化 switch-case（各 8 个 case） |
| `TranslateOverlayWindow::translateAndShow(parent, pixmap, texts, polygons, overlayPos, labelRect, onOverlayShown) → void` | TranslateOverlayWindow 静态 | SnipScreen、PinWindow 两处翻译流程中的：标签创建与居中、batchFinished + failed 一次性信号连接、Overlay 创建与定位、错误临时提示标签、翻译完成后回调（SnipScreen 退出截图框） |

---

## 10. 模块划分与文件清单

### 10.1 新增文件

| 文件 | 说明 |
|---|---|
| `src/translate/TranslateEngine.h/.cpp` | 抽象接口 + TranslateError 枚举 |
| `src/translate/MyMemoryEngine.h/.cpp` | 默认引擎（免注册） |
| `src/translate/BaiduTranslateEngine.h/.cpp` | 百度翻译（用户自填 Key） |
| `src/translate/DeepLEngine.h/.cpp` | DeepL（用户自填 Key） |
| `src/translate/LibreTranslateEngine.h/.cpp` | LibreTranslate（自托管 URL） |
| `src/translate/TranslateService.h/.cpp` | 单例 + 批量状态机 + checkEnabledAndPrivacy + errorMessage |
| `src/widgets/TranslateOverlayWindow.h/.cpp` | 叠加窗口 + 文字选择 + translateAndShow + onOverlayShown 回调 |
| `icons/translate.svg` | 翻译按钮图标（「文/A」设计） |

### 10.2 修改文件

| 文件 | 改动 |
|---|---|
| `src/ocr/OcrResultDialog.h/.cpp` | 翻译按钮、三态视图、失败弹窗（详细信息按钮） |
| `src/widgets/BaseToolBar.h/.cpp` | 加 translateRequested 信号 + m_translateBtn 公共成员 |
| `src/widgets/ScreenshotToolBar.cpp` | 创建翻译按钮并连接 translateRequested |
| `src/widgets/RecordingToolBar.cpp` | 创建翻译按钮并连接 translateRequested |
| `src/widgets/PinWindow.h/.cpp` | 右键加翻译项（顺序：复制→OCR→翻译→保存） |
| `src/capture/SnipScreen.h/.cpp` | 连接两个工具栏 translateRequested、performTranslate、隐私提示居中选区 |
| `src/widgets/SettingsWindow.h/.cpp` | 翻译选项卡（样式与历史记录之间） |
| `src/core/ConfigManager.cpp` | translate/ 默认配置初始化 |
| `src/languages/*.json`（6 个） | 加 32+ 翻译键 |
| `CMakeLists.txt` | Qt6::Network 模块 + 翻译源文件 + post-build TLS 插件部署 |
| `src/resources.qrc` | translate.svg 图标引用 |

---

## 11. 翻译键（i18n）完整清单

文件路径：`src/languages/{zh_CN,en_US,ja_JP,ko_KR,zh_TW,zh_HK}.json`

| 键 | 说明（zh_CN） |
|---|---|
| `tabTranslate` | 设置页「翻译」选项卡标题 |
| `ocr.translate` | OCR 弹窗「翻译」按钮文本 |
| `translate.button` | 工具栏「翻译」按钮文本 |
| `translate.translating` | 「翻译中...」加载提示 |
| `translate.viewMode` | 右键「视图模式」子菜单标题 |
| `translate.viewOriginal` | 原文视图 |
| `translate.viewTranslation` | 译文视图 |
| `translate.viewBoth` | 对照视图 |
| `translate.copyOriginal` | 复制原文 |
| `translate.copyTranslation` | 复制译文 |
| `translate.saveAsImage` | 另存为图片 |
| `translate.selectText` | 选择文字开关 |
| `translate.targetLang` | 设置页「目标语言」 |
| `translate.sourceLang` | 设置页「源语言」 |
| `translate.engine` | 设置页「翻译引擎」 |
| `translate.langAuto` | 自动检测 |
| `translate.engineMyMemory` | MyMemory（免注册） |
| `translate.engineBaidu` | 百度翻译 |
| `translate.engineDeepL` | DeepL |
| `translate.engineLibre` | LibreTranslate（自托管） |
| `translate.privacyTitle` | 隐私提示标题 |
| `translate.privacyMsg` | 隐私提示正文 |
| `translate.privacyDontAsk` | 不再提示复选框 |
| `translate.showDetails` | 失败弹窗「详细信息」按钮 |
| `translate.error.networkFailed` | 网络连接失败 |
| `translate.error.sslFailed` | SSL/TLS 错误 |
| `translate.error.sameLanguage` | 源目标语言相同 |
| `translate.error.rateLimit` | 额度用尽 |
| `translate.error.notConfigured` | 引擎未配置 |
| `translate.error.apiError` | 服务返回错误 |
| `translate.error.emptyText` | 文本为空 |
| `translate.error.unknown` | 未知错误 |

---

## 12. 风险与取舍

| 风险 | 影响 | 应对 |
|---|---|---|
| MyMemory 国内偶发不稳定 | 翻译失败 | 错误码友好提示；用户切换百度/自托管引擎 |
| MyMemory 额度耗尽 | 高频用户超额 | 设置引导填 email 提升额度；超额提示切换引擎 |
| 在线翻译隐私泄露 | 敏感文本外泄 | 首次隐私提示 + LibreTranslate 自托管引导 |
| 批量翻译请求多 | 慢速 + 高额度消耗 | 逐段顺序 + 失败回退原文；未来可优化短文本合并 |
| 译文长度与原文差异大 | 叠加排版错乱 | 自动换行 + 字号缩放 + 对照模式兜底 |
| 网络 API 变更 | 引擎失效 | 抽象接口隔离，单引擎失效不影响整体 |

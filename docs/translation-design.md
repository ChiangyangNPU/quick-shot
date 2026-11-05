# QuickShot 翻译功能技术文档

## 1. 概述

QuickShot 的翻译功能基于截图 OCR 识别结果，将识别出的文字翻译为目标语言。
提供两种使用形式：

| 形式 | 说明 | 使用场景 |
|---|---|---|
| OCR 结果弹窗翻译 | 在 OCR 识别结果弹窗底部点击「翻译」，弹窗内切换原文/译文/对照 | 查看 OCR 结果时顺带翻译 |
| 翻译结果窗口（左图右文分栏） | 在截图工具栏 / 录屏工具栏 / 钉图窗口右键点击「翻译」，弹出独立窗口：左侧显示原图、右侧显示可选中可滚动的译文 | 对比阅读，译文可自由选择复制 |

### 1.1 技术栈

| 组件 | 技术 | 说明 |
|------|------|------|
| 翻译引擎抽象 | 纯 Qt QObject 子类 | 统一异步 translate() 接口与错误码体系 |
| 默认引擎 | MyMemory 免费 API（免注册） | 国内可直连，带 email 提升额度至 50000 词/天 |
| 扩展引擎 | 百度翻译 / DeepL / LibreTranslate | 用户自填 Key / URL，软件不预置任何凭证 |
| 凭据存储 | SecretStore（Windows DPAPI / macOS Keychain / Linux 明文降级） | API Key 不再明文进 INI，老配置读路径自动迁移 |
| 网络请求 | Qt6::Network QNetworkAccessManager | HTTP GET/POST 视引擎而定（MyMemory/百度 GET，DeepL POST 表单编码，LibreTranslate POST JSON），响应均 JSON 解析 |
| TLS 支持 | Qt TLS 后端插件 post-build 部署 | 确保 HTTPS 请求正常 |
| 批量翻译 | 逐段顺序 + 失败回退原文 | 避免单段失败中断整体流程 |
| 国际化 | 项目 TranslationManager JSON 翻译键 | 6 种语言：zh_CN/en_US/ja_JP/ko_KR/zh_TW/zh_HK |

### 1.2 核心特性

- **默认开箱即用**：MyMemory 引擎免注册，首次使用弹出隐私提示
- **多引擎可切换**：设置页提供百度/DeepL/LibreTranslate 自填 Key 入口
- **弹窗内三态对照**：OcrResultDialog 内切换原文/译文/对照
- **左图右文分栏结果窗口**：TranslateOverlayWindow 左侧等比显示原图、右侧文本区可自由选中并滚动，视图模式仅切换右侧内容
- **窗口操作齐全**：标题栏拖动/双击关闭、右下角缩放抓手、ESC 关闭、复制全文/另存为图片、右键菜单复制原文/译文
- **错误码本地化**：9 种错误分类 → 查翻译键显示，「详细信息」保留原始报错
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
        D2["晾衣绳预览右键翻译"]
    end

    subgraph 翻译服务层
        E["TranslateService 单例"]
        E1["checkEnabledAndPrivacyAsync"]
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
        K["TranslateOverlayWindow 左图右文分栏"]
        K1["左栏 TranslateImageView 原图"]
        K2["右栏只读 QTextEdit 译文"]
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
    K -->|"视图模式切换右栏内容"| K2
    L --> E
    M --> L
```

### 2.2 分层说明

- **入口层**：覆盖所有触发点
- **翻译服务层**：`TranslateService` 单例，统一管理引擎实例、配置加载、批量翻译状态机、公共静态方法
- **引擎实现层**：抽象 `TranslateEngine` 接口 + 4 种具体实现
- **结果展示层**：`OcrResultDialog`（三态视图）与 `TranslateOverlayWindow`（左图右文分栏结果窗口）
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
        +translate(text, srcLang, tgtLang) void
        +finished(original, translated) signal
        +failed(code, detail) signal
        -sendRequest(text, srcLang, tgtLang) QNetworkReply*
        -parseResponse(data, outTranslated, outError, outDetail) bool*
    }

    note for TranslateEngine "模板方法结构：translate() 为基类非虚模板方法，统一处理空文本/未配置检查、原文记录、网络/SSL 错误分类、空译文兜底与结果回传；子类仅实现 sendRequest()（构造并发送 HTTP 请求，含语言映射与签名）与 parseResponse()（解析响应体）"

    class MyMemoryEngine {
        -m_email QString
        +setEmail(email) void
        +name() QString
        +isAvailable() bool
        +requiresApiKey() bool
        -sendRequest(text, srcLang, tgtLang) QNetworkReply*
        -parseResponse(data, outTranslated, outError, outDetail) bool
    }

    class BaiduTranslateEngine {
        -m_appId QString
        -m_key QString
        +setAppId(id) void
        +setKey(key) void
        +name() QString
        +isAvailable() bool
        +requiresApiKey() bool
        -sendRequest(text, srcLang, tgtLang) QNetworkReply*
        -parseResponse(data, outTranslated, outError, outDetail) bool
    }

    class DeepLEngine {
        -m_key QString
        +setKey(key) void
        +name() QString
        +isAvailable() bool
        +requiresApiKey() bool
        -sendRequest(text, srcLang, tgtLang) QNetworkReply*
        -parseResponse(data, outTranslated, outError, outDetail) bool
    }

    class LibreTranslateEngine {
        -m_url QString
        +setUrl(url) void
        +name() QString
        +isAvailable() bool
        +requiresApiKey() bool
        -sendRequest(text, srcLang, tgtLang) QNetworkReply*
        -parseResponse(data, outTranslated, outError, outDetail) bool
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
        -m_batchSourceLang QString
        -m_batchTargetLang QString
        +instance() TranslateService$
        +loadConfig(settings) void
        +setCurrentEngine(name) void
        +translate(text, srcLang, tgtLang) void
        +translateBatch(texts, srcLang, tgtLang) void
        +checkEnabledAndPrivacyAsync(parent, centerRect, onDecided) void$
        +errorMessage(code) QString$
        +finished(original, translated) signal
        +failed(code, detail) signal
        +batchFinished(translatedTexts) signal
        +currentEngineChanged(name) signal
        +onEngineFinished(original, translated) slot
        +onEngineFailed(code, detail) slot
        +translateNextBatchItem() slot
    }

    class TranslateOverlayWindow {
        -m_pixmap QPixmap
        -m_texts QStringList
        -m_translatedTexts QStringList
        -m_viewMode ViewMode
        -m_imageView TranslateImageView*
        -m_textView QTextEdit*
        +setViewMode(mode) void
        +viewMode() ViewMode
        +translateAndShow(parent, pixmap, texts, overlayPos, labelRect, onOverlayShown) void$
        +eventFilter(watched, event) bool
        +copyOriginal() void
        +copyTranslation() void
        +saveAsImage() void
        +showContextMenu(globalPos) void
        +updateTextContent() void
    }

    class OcrResultDialog {
        -m_fullText QString
        -m_translatedText QString
        -m_viewMode int
        -m_viewModeCombo QComboBox*
        +onTranslate() slot
        +onViewModeChanged(index) slot
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
    SnipScreen --> TranslateService : checkEnabledAndPrivacyAsync
    PinWindow --> TranslateService : checkEnabledAndPrivacyAsync
    OcrResultDialog --> TranslateService : checkEnabledAndPrivacyAsync, translate
```

---

## 4. 翻译引擎层设计

### 4.1 抽象接口 TranslateEngine

文件：[TranslateEngine.h](../src/translate/TranslateEngine.h)

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
    Busy,            ///< 翻译服务忙（批量翻译进行中，暂不接受新请求）
    Unknown          ///< 未知错误兜底
};
```

### 4.2 四种引擎对比

| 引擎 | 类型 | 配置项 | 国内可达 | 额度 | 说明 |
|---|---|---|---|---|---|
| MyMemoryEngine | 默认，免注册 | 可选 email | ✅ 直连 | 无 email:5000 词/天<br>有 email:50000 词/天 | 签名不加密，最容易使用 |
| BaiduTranslateEngine | Key-Based | appId + key | ✅ 国内服务 | 每月免费 200 万字符 | 签名：`MD5(appId+query+salt+key)` |
| DeepLEngine | Key-Based | key | ❌ 需联网 | 使用免费版端点 api-free.deepl.com（付费版另有 api.deepl.com，本软件用免费版） | 翻译质量最高 |
| LibreTranslateEngine | URL-Based | 服务地址 | ✅ 自托管可达 | 取决于自托管服务 | 完全离线可实现 |

Key / URL 由用户在设置页填写，**软件不预置任何凭证**。其中百度 AppId/Key、DeepL Key 经 **SecretStore** 加密存储（Windows DPAPI 密文 Base64 回写 INI `secure/` 前缀键、macOS Keychain、Linux 明文降级），旧明文配置在读取时自动迁移（TranslateService.cpp:165-167、SecretStore.h）；LibreTranslate URL、MyMemory email 等非敏感项仍存 QSettings。

---

## 5. 配置项设计

统一前缀 `translate/`，通过 `ConfigManager::getSettings()` 读写。

| 配置键 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `translate/enabled` | bool | true | 未启用时所有翻译入口不响应 |
| `translate/engine` | QString | "mymemory" | 当前引擎 |
| `translate/targetLang` | QString | "en" | 目标语言 |
| `translate/sourceLang` | QString | "auto" | 源语言（仅内部默认值，无 UI：设置页无源语言下拉，见 ConfigManager.cpp:172） |
| `translate/mymemoryEmail` | QString | "" | MyMemory 邮箱（提升额度） |
| `translate/baiduAppId` | QString | "" | 百度 App ID（经 SecretStore 加密存储，旧明文自动迁移） |
| `translate/baiduKey` | QString | "" | 百度 Key（经 SecretStore 加密存储，旧明文自动迁移） |
| `translate/deeplKey` | QString | "" | DeepL Key（经 SecretStore 加密存储，旧明文自动迁移） |
| `translate/libreUrl` | QString | "" | LibreTranslate 地址 |
| `translate/showPrivacyWarning` | bool | true | 首次隐私提示开关 |

> 凭据存储口径：百度 AppId/Key、DeepL Key 经 SecretStore 加密存储（Windows DPAPI 密文 Base64 回写 INI `secure/` 键、macOS Keychain、Linux 明文降级），旧明文读取时自动迁移；其余项存本地 QSettings。与 1.1 技术栈表一致。

### 5.1 SettingsWindow 翻译选项卡

位置在「样式」与「历史记录」之间：**常规 / 快捷键 / 样式 / 翻译 / 历史记录 / 宠物 / 关于**。

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
    Btn --> Check["TranslateService checkEnabledAndPrivacyAsync<br/>（异步：结果经 onDecided 回调返回）"]
    Check -->|"未启用 同步回调 onDecided(false)"| End1(["不处理"])
    Check -->|"首次使用"| Dlg["非模态隐私提示弹窗<br/>无父窗口 + 置顶 居中 不阻塞输入"]
    Dlg -->|"finished 回调 onDecided(false) 用户拒绝"| End2(["结束"])
    Dlg -->|"onDecided(true) 用户同意"| S1["translate 整段翻译"]
    Check -->|"已启用且已同意 onDecided(true)"| S1

    S1 --> S2["MyMemoryEngine HTTP GET"]
    S2 -->|"成功"| Ok["译文写入 m_translatedText 默认切换到译文视图"]
    S2 -->|"失败"| Err["弹窗显示 errorMessage 加详细信息按钮查看 detail"]

    Ok --> Switch["用户经视图模式下拉框切换"]
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
    D->>S: checkEnabledAndPrivacyAsync(this, rect, onDecided)
    alt 未启用
        S-->>D: 同步回调 onDecided(false) 不弹窗
    else 已启用且无需提示 showPrivacyWarning=false
        S-->>D: 同步回调 onDecided(true)
    else 首次使用且 showPrivacyWarning=true
        S->>S: 非模态弹隐私提示（无父窗口 置顶 不阻塞输入）
        U->>S: 点击 是 / 否
        S->>S: finished 回调：同意且勾选不再提示则置 showPrivacyWarning=false
        S-->>D: onDecided(true) 同意 / onDecided(false) 拒绝
    end

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
    译文 --> 原文 : 下拉框选择原文
    译文 --> 对照 : 下拉框选择对照
    原文 --> 对照 : 下拉框选择对照（有译文时）
    对照 --> 原文 : 下拉框选择原文
    对照 --> 译文 : 下拉框选择译文
```

- **切换控件**：标题栏下方的视图模式下拉框 QComboBox（`m_viewModeCombo`，三态：原文/译文/对照，OcrResultDialog.cpp:99-105），切换经 `onViewModeChanged(int)` 槽处理
- **原文视图**：纯 OCR 识别文本
- **译文视图**：纯翻译后文本
- **对照视图**：按段原文 + 译文上下间隔显示（或左右对照）

### 6.4 翻译粒度

- 整段翻译：把 `m_fullText`（OCR 结果按行合并）整体送译
- 优点：请求次数少、额度消耗低、实现简单

---

## 7. 形式二：翻译结果窗口（左图右文分栏）

### 7.1 四处入口

| 入口 | 触发方式 | OCR 时机 |
|---|---|---|
| 截图工具栏 | ScreenshotToolBar 点击翻译按钮 | 截取当前选区图像 → OCR |
| 录屏工具栏 | RecordingToolBar 点击翻译按钮 | 截取当前选区图像 → OCR |
| 钉图窗口右键 | PinWindow 右键菜单「翻译」（复制→OCR→翻译→保存） | 对钉图 m_pixmap → OCR |
| 晾衣绳预览右键 | 晾衣绳卡片悬停预览（CardPreviewPopup）右键菜单「翻译」 | 图片：对预览原图 → OCR（识别后统一释放模型）；文本：跳过 OCR，直接调用 TranslateService 弹译文结果框 |

> 钉图窗口右键菜单完整顺序为：复制→标注→OCR→翻译→保存。

### 7.2 完整流程图

```mermaid
flowchart TD
    Start(["点击翻译"]) --> GetPix["截取选区图像 / 取钉图图像"]
    GetPix --> Check{"checkEnabledAndPrivacyAsync<br/>parent centerRect 结果经 onDecided 回调"}
    Check -->|"未启用 同步回调 onDecided(false)"| End0(["不处理"])
    Check -->|"首次使用"| PrivacyDlg["非模态隐私提示弹窗 无父窗口置顶<br/>centerOn 居中到选区坐标"]
    PrivacyDlg -->|"finished 回调 onDecided(false) 拒绝"| End1(["结束"])
    PrivacyDlg -->|"onDecided(true) 同意"| OcrStart["异步OCR开始 显示识别中标签居中选区"]

    Check -->|"已启用且已同意 onDecided(true)"| OcrStart

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
        U1(["切换视图 仅影响右栏文本"])
        U2(["右侧文本区自由选中复制"])
        U3(["右键菜单 复制原文或译文"])
        U4(["底部操作栏 复制全文 另存为图片"])
        U5(["标题栏拖动 右下角缩放抓手"])
        U6(["ESC 关闭 标题栏双击关闭"])
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
    S->>TS: checkEnabledAndPrivacyAsync(this, selRect, onDecided)
    alt 首次使用
        TS-->>S: 非模态隐私提示弹窗（无父窗口置顶 居中 selRect）
        U->>S: 点击是
        TS-->>S: finished 回调 onDecided(true)
    else 已启用且无需提示
        TS-->>S: 同步回调 onDecided(true)
    end

    S->>S: 显示「识别中...」标签居中选区
    S->>O: recognize(image) 异步
    O-->>S: OcrResult(texts, polygons)
    alt texts 为空
        S->>S: 显示「未识别到文字」3秒临时标签
    else texts 非空
        S->>OW: translateAndShow(this, pixmap, texts, selPos, labelRect, onOverlayShown=exit)
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
            OW-->>U: 显示结果窗口
            OW->>S: onOverlayShown 回调
            S->>S: exit 退出截图框和工具栏
        else 引擎未配置等全局失败
            TS-->>OW: failed(code, detail)
            OW->>OW: 关闭翻译中标签
            OW->>OW: 显示 errorMessage 临时标签3秒
        end
    end

    Note over U,OW: === 结果窗口后续交互 ===
    U->>OW: 右键菜单 → 视图 仅原文/仅译文/对照
    OW->>OW: setViewMode 仅刷新右栏文本内容
    U->>OW: 右栏文本区鼠标/键盘选中 + Ctrl+C
    OW-->>U: 复制到剪贴板
    U->>OW: 底部「复制全文」/「另存为图片」
    OW->>OW: copyTranslation 复制译文全文 / saveAsImage 保存原图
    U->>OW: 标题栏拖动移动 / 双击关闭 / 右下角缩放抓手
    U->>OW: ESC
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
QString m_batchSourceLang;      // 批量源语言（进入批量时快照，逐段复用）
QString m_batchTargetLang;      // 批量目标语言（进入批量时快照，逐段复用）
```

另有 `currentEngineChanged(name)` 信号：`setCurrentEngine()` 切换并重建当前引擎后发出（TranslateService.cpp:233）。

### 7.5 TranslateOverlayWindow 分栏布局与视图切换

```mermaid
flowchart TB
    subgraph 窗口结构
        W["TranslateOverlayWindow"] --> TB["标题栏 标题 加 关闭按钮<br/>TranslateTitleBar 拖动移动 双击关闭"]
        W --> SP["QSplitter 水平 分栏线可拖动"]
        SP --> IV["左栏 TranslateImageView<br/>原图等比缩放 最小宽 120"]
        SP --> TP["右栏 textPanel 最小宽 240"]
        TP --> TV["只读 QTextEdit TextSelectableByMouse 加 Keyboard"]
        TP --> AB["底部操作栏 复制全文 加 另存为图片 加 QSizeGrip"]
    end

    subgraph 视图切换
        V1["setViewMode"] --> V2["updateTextContent 按模式刷新右栏"]
        V2 -->|"Original"| V3["右栏显示 OCR 原文"]
        V2 -->|"Translation 默认"| V4["右栏显示译文"]
        V2 -->|"Both"| V5["右栏按段显示原文 加 译文对照"]
    end

    subgraph 右键菜单
        R0["eventFilter 拦截原图控件与文本 viewport 的 ContextMenu"] --> R1["showContextMenu 统一弹出"]
        R1 --> R2["视图 仅原文/仅译文/对照"]
        R1 --> R3["复制原文 / 复制译文"]
        R1 --> R4["另存为图片"]
        R1 --> R5["关闭"]
    end
```

---

## 8. 隐私与错误处理

### 8.1 首次隐私提示定位策略

```mermaid
flowchart TD
    Start["checkEnabledAndPrivacyAsync parent centerRect onDecided 入口"] --> Enabled{"translate enabled?"}
    Enabled -->|"false"| CbFalse["同步回调 onDecided(false)"]
    Enabled -->|"true"| ShowWarning{"showPrivacyWarning?"}
    ShowWarning -->|"false"| CbTrue(["同步回调 onDecided(true)"])
    ShowWarning -->|"true"| CreateDlg["创建无父窗口 MessageBox<br/>WindowStaysOnTopHint 置顶 非模态 show 不阻塞 不 exec"]

    CreateDlg --> PosCalc{"centerRect isValid?"}
    PosCalc -->|"是 SnipScreen 传入选区"| Calc1["centerOn 弹窗中心等于 centerRect 中心"]
    PosCalc -->|"否 PinWindow 或 Dialog"| Calc2["centerOn 弹窗中心等于 parent frameGeometry 中心"]

    Calc1 --> Wait["等待用户点击 非模态不阻塞"]
    Calc2 --> Wait

    Wait --> Finished{"QDialog finished 回调"}
    Finished -->|"YES 加 勾选不再提示"| SaveCfg["translate showPrivacyWarning 置 false"] --> CbOk["回调 onDecided(true)"]
    Finished -->|"YES"| CbOk
    Finished -->|"NO"| CbNo["回调 onDecided(false)"]
```

> **实现要点**（TranslateService.cpp:37-107）：
> - 全平台统一"无父窗口 + 置顶 + 非模态"消息框：不阻塞输入、不改宿主窗口标志、不触发模态禁用/恢复链路（Windows 上应用模态框在置顶透明截图覆盖层之上开合会破坏鼠标派发）。
> - 结果不经返回值，而是经 `QDialog::finished` 回调调用 `onDecided(allowed)`；未启用或无需提示时**同步**回调。
> - macOS 附加处理：Qt 把带模态性的 QMessageBox 固定在模态面板级别(8)且手动抬级别会被重置，故弹框期间用 `Utils::beginModalOverOverlay`（setLevel 直接改合成层级、不重建窗口）把宿主置顶覆盖层临时降到普通级别，弹框关闭后在回调里恢复，避免被 101 级别的截图覆盖层挡住。

### 8.2 错误码 → 本地化消息对应表

| TranslateError | 翻译键 | zh_CN 示例 |
|---|---|---|
| NetworkFailed | `translate.errNetwork` | 网络连接失败，请检查网络后重试。 |
| SslFailed | `translate.errSsl` | SSL/TLS 未正确配置，无法建立加密连接，请检查 TLS 插件部署。 |
| SameLanguage | `translate.errSameLang` | 源语言与目标语言相同，请更换目标语言。 |
| RateLimit | `translate.errRateLimit` | 翻译额度已用尽，请稍后重试或更换引擎。 |
| NotConfigured | `translate.errNotConfigured` | 翻译引擎未配置，请在设置中完成配置。 |
| ApiError | `translate.errApi` | 翻译服务返回错误。 |
| EmptyText | `translate.errEmpty` | 没有可翻译的文本。 |
| Busy | `translate.errBusy` | 正在批量翻译中，请稍后再试。 |
| Unknown | `translate.errUnknown` | 翻译失败，请稍后重试。 |

> 映射实现：`TranslateService::errorMessage()` switch-case（TranslateService.cpp:118-135），共 9 个错误分类。

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
| `TranslateService::checkEnabledAndPrivacyAsync(parent, centerRect, onDecided) → void` | TranslateService 静态 | OcrResultDialog、SnipScreen、PinWindow 3 处各自实现的启用检查 + 隐私提示弹窗（异步回调式：非模态弹窗 + finished 回调 onDecided，未启用/无需提示时同步回调） |
| `TranslateService::errorMessage(code) → QString` | TranslateService 静态 | 3 处入口各自实现的 TranslateError → 本地化 switch-case（各 8 个 case） |
| `TranslateOverlayWindow::translateAndShow(parent, pixmap, texts, overlayPos, labelRect, onOverlayShown) → void` | TranslateOverlayWindow 静态 | SnipScreen、PinWindow 两处翻译流程中的：标签创建与居中、batchFinished + failed 一次性信号连接、结果窗口创建与定位、错误临时提示标签、翻译完成后回调（SnipScreen 退出截图框） |

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
| `src/translate/TranslateService.h/.cpp` | 单例 + 批量状态机 + checkEnabledAndPrivacyAsync + errorMessage |
| `src/widgets/TranslateOverlayWindow.h/.cpp` | 翻译结果窗口（左图右文分栏）+ translateAndShow + onOverlayShown 回调 |
| `icons/translate.svg` | 翻译按钮图标（「文/A」设计） |

### 10.2 修改文件

| 文件 | 改动 |
|---|---|
| `src/ocr/OcrResultDialog.h/.cpp` | 翻译按钮、三态视图、失败弹窗（详细信息按钮） |
| `src/widgets/BaseToolBar.h/.cpp` | 加 translateRequested 信号 + m_translateBtn 公共成员 |
| `src/widgets/ScreenshotToolBar.cpp` | 创建翻译按钮并连接 translateRequested |
| `src/widgets/RecordingToolBar.cpp` | 创建翻译按钮并连接 translateRequested |
| `src/widgets/PinWindow.h/.cpp` | 右键加翻译项（顺序：复制→标注→OCR→翻译→保存，PinWindow.cpp:455-488） |
| `src/capture/SnipScreen.h/.cpp` | 连接两个工具栏 translateRequested、performTranslate、隐私提示居中选区 |
| `src/widgets/SettingsWindow.h/.cpp` | 翻译选项卡（样式与历史记录之间） |
| `src/core/ConfigManager.cpp` | translate/ 默认配置初始化 |
| `src/languages/*.json`（6 个） | 加 50 个 `translate.*` 翻译键（第 11 节为主要键摘录，非穷举） |
| `CMakeLists.txt` | Qt6::Network 模块 + 翻译源文件 + post-build TLS 插件部署 |
| `src/resources.qrc` | translate.svg 图标引用 |

---

## 11. 翻译键（i18n）主要清单（非穷举）

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
| `translate.engine` | 设置页「翻译引擎」 |
| `translate.engineMyMemory` | MyMemory（免注册） |
| `translate.engineBaidu` | 百度翻译 |
| `translate.engineDeepL` | DeepL |
| `translate.engineLibre` | LibreTranslate（自托管） |
| `translate.privacyTitle` | 隐私提示标题 |
| `translate.privacyMsg` | 隐私提示正文 |
| `translate.privacyDontAsk` | 不再提示复选框 |
| `translate.showDetails` | 失败弹窗「详细信息」按钮 |
| `translate.errNetwork` | 网络连接失败 |
| `translate.errSsl` | SSL/TLS 错误 |
| `translate.errSameLang` | 源目标语言相同 |
| `translate.errRateLimit` | 额度用尽 |
| `translate.errNotConfigured` | 引擎未配置 |
| `translate.errApi` | 服务返回错误 |
| `translate.errEmpty` | 文本为空 |
| `translate.errBusy` | 批量翻译进行中 |
| `translate.errUnknown` | 未知错误 |

> **非穷举清单**：zh_CN.json 实际共 **50 个 `translate.*` 键**，上表为主要摘录。未列出的还有：`translate.enabled`、`translate.failed`、`translate.loading`、`translate.copy`、`translate.selectAll`、`translate.options`、`translate.privacyWarning`（均为设置页/右键菜单标签），`translate.{mymemory,baidu,deepl,libre}NetworkHint`（4 个引擎网络提示）、`translate.{mymemory,baidu,deepl,libre}WebsiteLink`（4 个引擎官网链接）、`translate.mymemoryEmailHint`，以及 4 个引擎配置键标签 `translate.baiduAppId`/`translate.baiduKey`/`translate.deeplKey`/`translate.libreUrl`。
>
> **无「源语言」UI**：设置页翻译选项卡仅提供目标语言下拉（SettingsTranslateTab.cpp:174-188），没有源语言下拉；`translate/sourceLang`（ConfigManager.cpp:172）仅为配置内部默认值（"auto"），无对应 i18n 键（早期清单中的「源语言」「自动检测」两个翻译键已删除，6 个语言文件均不存在）。

---

## 12. 风险与取舍

| 风险 | 影响 | 应对 |
|---|---|---|
| MyMemory 国内偶发不稳定 | 翻译失败 | 错误码友好提示；用户切换百度/自托管引擎 |
| MyMemory 额度耗尽 | 高频用户超额 | 设置引导填 email 提升额度；超额提示切换引擎 |
| 在线翻译隐私泄露 | 敏感文本外泄 | 首次隐私提示 + LibreTranslate 自托管引导 |
| 批量翻译请求多 | 慢速 + 高额度消耗 | 逐段顺序 + 失败回退原文；未来可优化短文本合并 |
| 译文长度与原文差异大 | 右栏内容过长 | 右栏可滚动查看；对照模式逐段对应兜底 |
| 网络 API 变更 | 引擎失效 | 抽象接口隔离，单引擎失效不影响整体 |

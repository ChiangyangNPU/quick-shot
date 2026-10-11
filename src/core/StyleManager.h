#ifndef STYLEMANAGER_H
#define STYLEMANAGER_H

#include <QString>
#include <QColor>
#include <QIcon>
#include <QSize>
#include <array>

// 前置声明，避免头文件级循环依赖：
// ConfigManager.cpp include StyleManager.h（用 DEFAULT_* 播种配置），
// StyleManager.cpp include ConfigManager.h（initFromConfig 读取持久化）。
// 两边的 include 都只发生在 .cpp，头文件仅前向声明。
class ConfigManager;
class QWidget;
class QPushButton;
class QComboBox;
class QCheckBox;
class QSpinBox;
class QLineEdit;
class QProgressBar;
class QGroupBox;
class QTabWidget;

/**
 * @brief 样式管理器类
 * 
 * 集中管理应用程序中各种 UI 组件的样式，包括按钮、工具栏等
 * 提供统一的样式字符串，确保界面风格一致性
 * @author chiangyang
 */
class StyleManager {
private:
    // 颜色设置

public:
    static QString s_toolbarButtonStyle;        ///< 工具栏按钮当前样式（"text"或"icon"）

    // 默认颜色
    static const QColor DEFAULT_RECORD_BORDER_COLOR;              ///< 录屏框默认颜色
    static const QColor DEFAULT_CAPTURE_BORDER_COLOR;             ///< 截屏框默认颜色
    static const QColor DEFAULT_TOOLBAR_BG_COLOR;                 ///< 工具栏默认背景颜色
    static const QColor DEFAULT_RECORD_CONTROL_BG_COLOR;          ///< 录屏控制栏默认背景颜色
    static const QColor DEFAULT_TOOLBAR_BTN_COLOR;                ///< 工具栏按钮默认颜色
    static const QColor DEFAULT_TOOLBAR_TEXT_COLOR;               ///< 工具栏按钮文字默认颜色
    static const QColor DEFAULT_TOOLBAR_BUTTON_HOVER_COLOR;       ///< 工具栏按钮悬停默认颜色
    static const QColor DEFAULT_TOOLBAR_BUTTON_DISABLED_COLOR;    ///< 工具栏按钮禁用默认颜色
    static const QColor DEFAULT_SUB_TOOLBAR_BG_COLOR;             ///< 子工具栏默认背景颜色
    static const QColor DEFAULT_SETTING_BUTTON_BG_COLOR;          ///< 设置窗口按钮默认背景颜色
    static const QColor DEFAULT_SETTING_BUTTON_TEXT_COLOR;        ///< 设置窗口按钮文字默认颜色
    static const QColor DEFAULT_TOOLBAR_BUTTON_CHECKED_COLOR;     ///< 工具栏按钮选中默认颜色
    static const QColor DEFAULT_CLOSE_BUTTON_BG_COLOR;            ///< 关闭按钮默认背景颜色
    static const QColor DEFAULT_CLOSE_BUTTON_HOVER_COLOR;         ///< 关闭按钮默认悬停颜色
    static const QColor DEFAULT_TAB_WIDGET_BG_COLOR;              ///< 选项卡背景默认颜色
    static const QColor DEFAULT_TAB_BUTTON_BG_COLOR;              ///< 选项卡按钮背景默认颜色
    static const QColor DEFAULT_TAB_BUTTON_TEXT_COLOR;            ///< 选项卡按钮文字默认颜色
    static const QColor DEFAULT_TAB_BUTTON_SELECTED_BG_COLOR;     ///< 选项卡按钮选中背景默认颜色
    static const QColor DEFAULT_TAB_BUTTON_SELECTED_TEXT_COLOR;   ///< 选项卡按钮选中文字默认颜色
    static const QColor DEFAULT_HANDLE_CIRCLE_COLOR;              ///< 角手柄圆形默认颜色
    static const QColor DEFAULT_HANDLE_CLOSE_COLOR;               ///< 角手柄关闭按钮默认颜色

    static const QString DEFAULT_TOOLBAR_BUTTON_STYLE;            ///< 工具栏按钮默认样式（文字模式）

    // ============ 颜色配置元数据（数据驱动单一数据源） ============
    // 此表是颜色/样式配置的唯一来源：ConfigManager 播种默认值、
    // SettingsWindow 构建设置 UI、initFromConfig 从持久化恢复均遍历此表。
    // 与 ShortcutTypes.h 的 kShortcutConfigs 保持同一"数据驱动"惯用法。
    // @author chiangyang

    /// 颜色配置 ID
    enum class StyleColorId {
        CaptureBorder, RecordBorder,
        ToolbarBg, SubToolbarBg, RecordControlBg, ToolbarBtn, ToolbarText,
        ToolbarButtonHover, ToolbarButtonDisabled, ToolbarButtonChecked,
        CloseButtonBg, CloseButtonHover,
        SettingButtonBg, SettingButtonText,
        TabWidgetBg, TabButtonBg, TabButtonText, TabButtonSelectedBg, TabButtonSelectedText,
        HandleCircle, HandleClose,
        Count
    };

    /// 颜色变更后的样式联动分类（决定 SettingsWindow 刷新哪些控件）
    enum class StyleColorCategory { Border, Toolbar, TabButton, TabWidgetBg };

    /// 颜色变更需发射的信号（仅保留有订阅者的信号）
    enum class StyleColorSignal { None, TabWidgetBg };

    /// 单个颜色配置项元数据
    struct StyleColorSetting {
        StyleColorId id;                     ///< 颜色 ID
        StyleColorCategory category;         ///< 联动分类
        StyleColorSignal signalId;           ///< 关联信号
        const char* settingsKey;             ///< QSettings 子键（如 "recordBorderColor"）
        const char* translationKey;          ///< 翻译键（如 "style.recordBorderColor"）
        const char* defaultText;             ///< 默认翻译文本（空串表示无 UI）
        QColor (*getter)();                  ///< 当前色 getter（StyleManager 静态方法）
        void (*setter)(const QColor&);       ///< 当前色 setter（StyleManager 静态方法）
        const QColor& defaultColor;          ///< 默认色（引用 StyleManager::DEFAULT_*）
    };

    static constexpr int kStyleColorCount = static_cast<int>(StyleColorId::Count); ///< 颜色配置项总数

    /**
     * @brief 颜色配置元数据表（Meyers Singleton）
     * @return 包含所有颜色配置项的静态数组引用
     * @note 单一数据源：ConfigManager 播种、SettingsWindow 建 UI、
     *       initFromConfig 从持久化恢复均遍历此表
     * @author chiangyang
     */
    static const std::array<StyleColorSetting, kStyleColorCount>& colorSettingTable();

    /**
     * @brief 读取指定颜色的当前值（表驱动统一入口）
     * @param id 颜色 ID
     * @return 当前颜色
     * @author chiangyang
     */
    static QColor color(StyleColorId id);

    /**
     * @brief 设置指定颜色的当前值（不触发样式刷新，联动由调用方负责）
     * @param id 颜色 ID
     * @param color 新颜色
     * @author chiangyang
     */
    static void setColor(StyleColorId id, const QColor &color);

    /**
     * @brief 从配置初始化样式（依赖注入）
     * @param cm ConfigManager 指针；nullptr 时重置为默认值
     * @note 应用启动时在 ConfigManager::setInstance 之后、任何窗口创建之前调用一次，
     *       使样式恢复与窗口构造顺序解耦（不再依赖 SettingsWindow 先于 SnipScreen）
     * @author chiangyang
     */
    static void initFromConfig(ConfigManager* cm);

    // 标注工具默认值
    static constexpr int DEFAULT_PEN_WIDTH = 5;                 ///< 画笔默认粗细
    static constexpr int DEFAULT_FONT_SIZE = 28;                ///< 文本默认字号
    static constexpr int DEFAULT_ERASER_WIDTH = 5;              ///< 橡皮擦默认粗细
    static constexpr int DEFAULT_MOSAIC_SIZE = 5;               ///< 马赛克默认大小

    static int s_defaultPenWidth;                               ///< 当前画笔默认粗细
    static int s_defaultFontSize;                               ///< 当前文本默认字号
    static int s_defaultEraserWidth;                            ///< 当前橡皮擦默认粗细
    static int s_defaultMosaicSize;                            ///< 当前马赛克默认大小

    static constexpr const int SNIP_BORDER_WIDTH = 2;             ///< 截图边框宽度


    /**
     * @brief 获取工具栏背景样式
     * @return 工具栏背景样式字符串
     * @note 使用位置: BaseCaptureToolBar（截图/录屏工具栏背景）、RecordingControlWindow（录制控制窗口背景）
     * @author chiangyang
     */
    static QString getToolbarBackgroundStyle();

    /**
     * @brief 获取子工具栏背景样式
     * @return 子工具栏背景样式字符串
     * @note 使用位置: BaseToolBar（子工具栏，如形状选择、颜色选择等）
     * @author chiangyang
     */
    static QString getSubToolbarStyle();

    /**
     * @brief 获取工具按钮样式（无状态）
     * @return 工具按钮样式字符串
     * @note 使用位置: 
     *   - BaseCaptureToolBar: 矩形、箭头、画笔、文本、马赛克、橡皮擦按钮
     *   - RecordingToolBar: 录屏按钮、截图按钮
     * @author chiangyang
     */
    static QString getToolButtonStyle();

    /**
     * @brief 获取操作按钮样式（包括悬停、选中、禁用状态）
     * @return 操作按钮样式字符串
     * @note 使用位置: 
     *   - BaseCaptureToolBar: 撤销、重做、清除按钮
     *   - ScreenshotToolBar: 录制、贴图、复制
     *   - RecordingControlWindow: 开始、暂停、继续、停止
     * @author chiangyang
     */
    static QString getActionButtonStyle();

    /**
     * @brief 获取关闭按钮样式
     * @return 关闭按钮样式字符串
     * @note 使用位置: 
     *   - ScreenshotToolBar: 关闭按钮（红色背景）
     *   - RecordingToolBar: 取消录屏按钮
     * @author chiangyang
     */
    static QString getCloseButtonStyle();

    /**
     * @brief 应用工具按钮样式到指定按钮
     * @param button 目标按钮
     * @note 使用位置: BaseCaptureToolBar、RecordingToolBar 中的工具按钮
     * @author chiangyang
     */
    static void applyToolButtonStyle(QPushButton *button);

    /**
     * @brief 应用操作按钮样式到指定按钮
     * @param button 目标按钮
     * @note 使用位置: BaseCaptureToolBar、ScreenshotToolBar、RecordingControlWindow 中的操作按钮
     * @author chiangyang
     */
    static void applyActionButtonStyle(QPushButton *button);

    /**
     * @brief 应用关闭按钮样式到指定按钮
     * @param button 目标按钮
     * @note 使用位置: ScreenshotToolBar、RecordingToolBar 中的关闭/取消按钮
     * @author chiangyang
     */
    static void applyCloseButtonStyle(QPushButton *button);

    /**
     * @brief 应用普通按钮样式到指定按钮（包含所有状态：普通、悬停、选中、禁用）
     * @param button 目标按钮
     * @note 使用位置: RecordingControlWindow 中的开始、暂停、继续、停止、浏览按钮
     * @note 内部调用 getToolButtonStyle，统一所有按钮样式
     * @author chiangyang
     */
    static void applyNormalButtonStyle(QPushButton *button);

    /**
     * @brief 获取按钮选中状态样式
     * @return 选中状态样式字符串
     * @note 使用位置: BaseCaptureToolBar（工具按钮选中时的高亮效果）
     * @author chiangyang
     */
    static QString getButtonCheckedStyle();

    /**
     * @brief 获取下拉框样式
     * @return 下拉框样式字符串
     * @note 使用位置: 
     *   - BaseToolBar: 形状类型选择下拉框
     *   - RecordingControlWindow: 分辨率选择下拉框
     * @author chiangyang
     */
    static QString getComboBoxStyle();

    /**
     * @brief 获取复选框样式
     * @return 复选框样式字符串
     * @note 使用位置:
     *   - RecordingControlWindow: 音频录制选项复选框
     * @author chiangyang
     */
    static QString getCheckBoxStyle();

    /**
     * @brief 获取设置/历史界面复选框样式
     * @return 复选框样式字符串
     * @note 使用位置: SettingsWindow（开机自启、日志打印、OCR GPU加速、
     *       翻译开关、隐私提示、截图/剪贴板历史记录开关等复选框）
     *       显式指定文字颜色为黑色，避免不同系统主题下文字颜色
     *       与浅色背景相近导致看不见（深色系统主题下Qt原生
     *       QCheckBox会把文字继承为浅色，在浅色背景上不可见）。
     *       配色与 getPathEditStyle() 保持一致（黑字）。
     * @author chiangyang
     */
    static QString getSettingsCheckBoxStyle();

    /**
     * @brief 获取颜色按钮样式
     * @param color 颜色值
     * @param isSelected 是否选中
     * @return 颜色按钮样式字符串
     * @note 使用位置: BaseToolBar（颜色选择器中的颜色按钮，选中时有边框效果）
     * @author chiangyang
     */
    static QString getColorButtonStyle(const QString &color, bool isSelected = false);

    /**
     * @brief 获取截图文本编辑框样式
     * @param color 文字和边框颜色
     * @param fontSize 字体像素大小
     * @return 截图文本编辑框样式字符串
     * @note 使用位置: OverlayTextEdit（截图上的浮动文本输入框）
     * @author chiangyang
     */
    static QString getOverlayTextEditStyle(const QColor &color, int fontSize);

    /**
     * @brief 获取选区尺寸信息标签样式
     * @return 选区尺寸信息标签样式字符串
     * @note 使用位置: Selector（选区左上角的尺寸提示标签）
     * @author chiangyang
     */
    static QString getSnipInfoLabelStyle();

    /**
     * @brief 获取录制时间标签样式
     * @return 录制时间标签样式字符串
     * @note 使用位置: Selector（选区左上角的录制时间标签，显示在尺寸标签上方）
     * @author chiangyang
     */
    static QString getRecordTimerLabelStyle();

    /**
     * @brief 获取PinWindow首次提示标签样式
     * @return PinWindow首次提示标签样式字符串
     * @note 使用位置: PinWindow（首次显示时的操作提示标签）
     * @author chiangyang
     */
    static QString getPinHintLabelStyle();

    /**
     * @brief 获取OCR识别加载提示标签样式
     * @return OCR识别加载提示标签样式字符串
     * @note 使用位置: 
     *   - SnipScreen（截图模式下OCR识别中的加载提示）
     *   - PinWindow（PinWindow模式下OCR识别中的加载提示）
     * @author chiangyang
     */
    static QString getOcrLoadingLabelStyle();

    /**
     * @brief 获取OCR结果文本显示框样式
     * @return OCR结果文本显示框样式字符串
     * @note 使用位置: OcrResultDialog（OCR识别结果文本显示区域）
     * @author chiangyang
     */
    static QString getOcrResultTextStyle();

    /**
     * @brief 获取OCR结果标题样式
     * @return OCR结果标题样式字符串
     * @note 使用位置: OcrResultDialog（OCR识别结果标题）
     * @author chiangyang
     */
    static QString getOcrResultTitleStyle();

    /**
     * @brief 获取应用名称标签样式
     * @return 应用名称标签样式字符串
     * @note 使用位置: SettingsWindow（设置窗口中的应用名称标签）
     * @author chiangyang
     */
    static QString getAppNameLabelStyle();

    /**
     * @brief 获取菜单样式
     * @return 菜单样式字符串
     * @note 使用位置: PinWindow（右键菜单）、托盘区
     * @author chiangyang
     */
    static QString getMenuStyle();

    /**
     * @brief 获取窗口样式（带文字颜色）
     * @return 窗口样式字符串
     * @note 使用位置: RecordingControlWindow（录制控制窗口）
     * @author chiangyang
     */
    static QString getWindowStyle();
    
    /**
     * @brief 获取路径输入框样式
     * @return 路径输入框样式字符串
     * @note 使用位置: SettingsWindow（保存路径显示）
     * @author chiangyang
     */
    static QString getPathEditStyle();
    
    /**
     * @brief 获取快捷键输入框样式
     * @return 快捷键输入框样式字符串
     * @note 使用位置: SettingsWindow（快捷键设置）
     * @author chiangyang
     */
    static QString getKeySequenceEditStyle();

    /**
     * @brief 获取分组框样式
     * @return 分组框样式字符串
     * @note 尺寸（border-radius/margin/padding）由全局 qss 用 em 单位管理，
     *       此处只返回颜色相关样式（用户可配置的背景色）
     * @author chiangyang
     */
    static QString getGroupBoxStyle();
    
    /**
     * @brief 获取设置窗口按钮样式
     * @return 设置窗口按钮样式字符串
     * @note 使用位置: SettingsWindow（快捷键设置按钮）
     *       尺寸由全局 qss 用 em 单位管理，此处只返回颜色相关样式
     * @author chiangyang
     */
    static QString getSettingsButtonStyle();

    /**
     * @brief 获取设置/历史界面下拉框与数字框样式
     * @return 下拉框/数字框样式字符串
     * @note 使用位置: SettingsWindow（语言、OCR语言、翻译引擎、翻译目标语言、
     *       工具栏按钮样式、历史保留天数、最大条数等下拉框、标注工具默认值数字框）、
     *       HistoryWindow（时间筛选下拉框）
     *       尺寸（height/border-radius/padding）由全局 qss 用 em 单位管理，
     *       此处只返回颜色及 drop-down/arrow 相关样式，
     *       避免不同系统原生渲染差异导致跨电脑显示不一致。
     *       配色与 getPathEditStyle() 保持一致（白底黑字）。
     *       同时覆盖 QSpinBox/QDoubleSpinBox（QAbstractSpinBox）：统一输入框区域
     *       背景/边框/文字，上下箭头用图片绘制（spinner-up/down.svg）。
     *       Qt 的 spinbox 箭头只认 image:，不渲染 combobox 那种 CSS 边框三角，
     *       必须给 ::up-button/::down-button 明确宽度并配真实箭头图片，否则箭头消失。
     * @author chiangyang
     */
    static QString getSettingsComboBoxStyle();

    /**
     * @brief 获取消息框样式
     * @return 消息框样式字符串
     * @note 使用位置: 项目所有 QMessageBox（确认对话框、警告、信息提示、翻译隐私提示等）
     *       统一消息框视觉风格，背景与 GroupBox 一致，按钮配色参考设置窗口按钮
     *       尺寸由全局 qss 用 em 单位管理，此处只返回颜色相关样式
     * @author chiangyang
     */
    static QString getMessageBoxStyle();
    
    /**
     * @brief 获取进度条样式
     * @return 进度条样式字符串
     * @note 使用位置: SettingsWindow（检查更新下载进度）
     *       尺寸由全局 qss 用 em 单位管理，此处只返回颜色相关样式
     *       进度条填充色与设置按钮悬停色保持一致，背景色与选项卡背景一致
     * @author chiangyang
     */
    static QString getProgressBarStyle();
    
    /**
     * @brief 获取选项卡控件样式
     * @return 选项卡控件样式字符串
     * @note 使用位置: SettingsWindow（选项卡控件）、HistoryWindow（全部/截图/文本选项卡）
     *       尺寸由全局 qss 用 em 单位管理，此处只返回颜色相关样式
     * @author chiangyang
     */
    static QString getTabWidgetStyle();

    /**
     * @brief 加载SVG图标
     * @param iconPath SVG图标路径
     * @param size 图标大小
     * @return 加载的图标
     * @author chiangyang
     */
    static QIcon loadSvgIcon(const QString &iconPath, const QSize &size);

    /**
     * @brief 加载SVG图标
     * @param iconPath SVG图标路径
     * @return 加载的图标
     * @author chiangyang
     */
    static QIcon loadSvgIcon(const QString &iconPath);

    /**
     * @brief 加载应用图标
     * @return 加载的应用图标
     * @author chiangyang
     */
    static QIcon loadAppIcon();

    /**
     * @brief 工具栏圆角半径（对齐 QSS border-radius: 0.3em 的视觉意图）
     *
     * 以控件字体的 'M' 字符宽度近似 em。主工具栏/子工具栏/贴图工具栏
     * 三处绘制共用此实现，避免公式各自漂移。
     * @param widget 目标控件（取其字体度量）
     * @return 圆角半径（像素）
     * @author chiangyang
     */
    static int toolbarCornerRadius(const QWidget *widget);

    /**
     * @brief 下拉框弹窗统一为"高亮选中"风格（去 ✓ 勾选指示器）
     *
     * 平台差异：macOS 风格的弹窗是菜单式的——当前项前绘制 ✓ 且弹窗锚在
     * combo 顶部；Windows 风格是列表式的——当前项用高亮背景、弹窗在下方。
     * 全应用的 QSS 已用 item:selected 高亮表达选中，安装本代理后各平台
     * 行为一致（✓ 列不再挤压文字宽度）。
     * @param combo 目标下拉框
     * @author chiangyang
     */
    static void useHighlightOnlyPopup(QComboBox *combo);

    /**
     * @brief 重新应用全局样式表
     *
     * 全局 qss（app.qss）只在 setStyleSheet() 调用时解析一次，DPI 变化后
     * pt（字体）和 em（控件尺寸）不会自动更新。此方法重新加载并应用
     * 全局 qss，让 pt 按新 logicalDotsPerInch 重新换算像素、em 基于新
     * 字体重新计算，确保 DPI 变化后所有窗口的字体和控件尺寸正确更新。
     * 在 SettingsWindow 和 HistoryWindow 的 DPI 变化处理中均会调用。
     * @return 是否成功重新应用
     * @author chiangyang
     */
    static bool reapplyGlobalStyleSheet();

    // ============ 逐色 getter/setter（公共 API 保留，实现为一行委托，见 StyleManager.cpp） ============
    // 21 个颜色的当前值统一存于 colorStore()（StyleManager.cpp 内 Meyers 单例，按 StyleColorId 顺序）。
    // 表内条目经 colorSettingTable() 的函数指针引用这些函数；部分 getter 亦被
    // Selector/SnipScreen/OverlayTextEdit 等直接调用。

    // 截屏框颜色
    static QColor getCaptureBorderColor();
    static void setCaptureBorderColor(const QColor &color);

    // 录屏框颜色
    static QColor getRecordBorderColor();
    static void setRecordBorderColor(const QColor &color);

    // 工具栏背景颜色
    static QColor getToolbarBgColor();
    static void setToolbarBgColor(const QColor &color);

    // 子工具栏背景颜色
    static QColor getSubToolbarBgColor();
    static void setSubToolbarBgColor(const QColor &color);

    // 录屏控制栏背景颜色
    static QColor getRecordControlBgColor();
    static void setRecordControlBgColor(const QColor &color);

    // 工具栏按钮颜色
    static QColor getToolbarBtnColor();
    static void setToolbarBtnColor(const QColor &color);

    // 工具栏按钮文字颜色
    static QColor getToolbarTextColor();
    static void setToolbarTextColor(const QColor &color);

    // 工具栏按钮悬停颜色
    static QColor getToolbarButtonHoverColor();
    static void setToolbarButtonHoverColor(const QColor &color);

    // 工具栏按钮禁用颜色
    static QColor getToolbarButtonDisabledColor();
    static void setToolbarButtonDisabledColor(const QColor &color);

    // 工具栏按钮选中颜色
    static QColor getToolbarButtonCheckedColor();
    static void setToolbarButtonCheckedColor(const QColor &color);

    // 关闭按钮背景颜色
    static QColor getCloseButtonBgColor();
    static void setCloseButtonBgColor(const QColor &color);

    // 关闭按钮悬停颜色
    static QColor getCloseButtonHoverColor();
    static void setCloseButtonHoverColor(const QColor &color);

    // 设置窗口按钮背景颜色
    static QColor getSettingButtonBgColor();
    static void setSettingButtonBgColor(const QColor &color);

    // 设置窗口按钮文字颜色
    static QColor getSettingButtonTextColor();
    static void setSettingButtonTextColor(const QColor &color);

    // 选项卡背景颜色
    static QColor getTabWidgetBgColor();
    static void setTabWidgetBgColor(const QColor &color);

    // 选项卡按钮背景颜色
    static QColor getTabButtonBgColor();
    static void setTabButtonBgColor(const QColor &color);

    // 选项卡按钮文字颜色
    static QColor getTabButtonTextColor();
    static void setTabButtonTextColor(const QColor &color);

    // 选项卡按钮选中背景颜色
    static QColor getTabButtonSelectedBgColor();
    static void setTabButtonSelectedBgColor(const QColor &color);

    // 选项卡按钮选中文字颜色
    static QColor getTabButtonSelectedTextColor();
    static void setTabButtonSelectedTextColor(const QColor &color);

    // 角手柄圆形颜色
    static QColor getHandleCircleColor();
    static void setHandleCircleColor(const QColor &color);

    // 角手柄关闭按钮颜色
    static QColor getHandleCloseColor();
    static void setHandleCloseColor(const QColor &color);

    /**
     * @brief 获取工具栏按钮样式
     * @return 工具栏按钮样式（"text"或"icon"）
     * @author chiangyang
     */
    static QString getToolbarButtonStyle();
    
    /**
     * @brief 设置工具栏按钮样式
     * @param style 工具栏按钮样式（"text"或"icon"）
     * @author chiangyang
     */
    static void setToolbarButtonStyle(const QString &style);

    /**
     * @brief 获取画笔默认粗细
     * @return 画笔默认粗细值（像素）
     * @author chiangyang
     */
    static int getDefaultPenWidth();

    /**
     * @brief 设置画笔默认粗细
     * @param width 画笔默认粗细值（像素）
     * @author chiangyang
     */
    static void setDefaultPenWidth(int width);

    /**
     * @brief 获取文本默认字号
     * @return 文本默认字号（像素）
     * @author chiangyang
     */
    static int getDefaultFontSize();

    /**
     * @brief 设置文本默认字号
     * @param size 文本默认字号（像素）
     * @author chiangyang
     */
    static void setDefaultFontSize(int size);

    /**
     * @brief 获取橡皮擦默认粗细
     * @return 橡皮擦默认粗细值（像素）
     * @author chiangyang
     */
    static int getDefaultEraserWidth();

    /**
     * @brief 设置橡皮擦默认粗细
     * @param width 橡皮擦默认粗细值（像素）
     * @author chiangyang
     */
    static void setDefaultEraserWidth(int width);

    /**
     * @brief 获取马赛克默认大小
     * @return 马赛克默认大小值（像素）
     * @author chiangyang
     */
    static int getDefaultMosaicSize();

    /**
     * @brief 设置马赛克默认大小
     * @param size 马赛克默认大小值（像素）
     * @author chiangyang
     */
    static void setDefaultMosaicSize(int size);
    
    /**
     * @brief 重置样式为默认值
     * @author chiangyang
     */
    static void resetToDefaults();
    
};

#endif // STYLEMANAGER_H

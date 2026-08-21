#ifndef STYLEMANAGER_H
#define STYLEMANAGER_H

#include <QString>
#include <QPushButton>
#include <QColor>
#include <array>

// 前置声明，避免头文件级循环依赖：
// ConfigManager.cpp include StyleManager.h（用 DEFAULT_* 播种配置），
// StyleManager.cpp include ConfigManager.h（initFromConfig 读取持久化）。
// 两边的 include 都只发生在 .cpp，头文件仅前向声明。
class ConfigManager;

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
    static QColor s_recordBorderColor;         ///< 录屏框当前颜色
    static QColor s_captureBorderColor;        ///< 截屏框当前颜色
    static QColor s_toolbarBgColor;            ///< 工具栏当前背景颜色
    static QColor s_recordControlBgColor;      ///< 录屏控制栏当前背景颜色
    static QColor s_toolbarBtnColor;           ///< 工具栏按钮当前颜色
    static QColor s_toolbarTextColor;          ///< 工具栏按钮文字当前颜色
    static QColor s_toolbarButtonHoverColor;   ///< 工具栏按钮悬停当前颜色
    static QColor s_toolbarButtonDisabledColor; ///< 工具栏按钮禁用当前颜色
    static QColor s_subToolbarBgColor;         ///< 子工具栏当前背景颜色
    static QColor s_settingButtonBgColor;      ///< 设置窗口按钮当前背景颜色
    static QColor s_settingButtonTextColor;    ///< 设置窗口按钮文字当前颜色
    static QColor s_toolbarButtonCheckedColor; ///< 工具栏按钮选中当前颜色
    static QColor s_closeButtonBgColor;        ///< 关闭按钮当前背景颜色
    static QColor s_closeButtonHoverColor;     ///< 关闭按钮当前悬停颜色
    static QColor s_tabWidgetBgColor;          ///< 选项卡背景当前颜色
    static QColor s_tabButtonBgColor;          ///< 选项卡按钮背景当前颜色
    static QColor s_tabButtonTextColor;        ///< 选项卡按钮文字当前颜色
    static QColor s_tabButtonSelectedBgColor;  ///< 选项卡按钮选中背景当前颜色
    static QColor s_tabButtonSelectedTextColor; ///< 选项卡按钮选中文字当前颜色
    static QColor s_handleCircleColor;         ///< 文本编辑框角手柄圆形当前颜色
    static QColor s_handleCloseColor;          ///< 文本编辑框角手柄关闭按钮当前颜色

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
    static QString getToolbarBackgroundStyle() {
        return QString("background-color: %1; border-radius: 0.3em;")
            .arg(s_toolbarBgColor.name());
    }

    /**
     * @brief 获取子工具栏背景样式
     * @return 子工具栏背景样式字符串
     * @note 使用位置: BaseToolBar（子工具栏，如形状选择、颜色选择等）
     * @author chiangyang
     */
    static QString getSubToolbarStyle() {
        return QString("background-color: %1; border-radius: 0.3em;")
            .arg(s_subToolbarBgColor.name());
    }

    /**
     * @brief 获取工具按钮样式（无状态）
     * @return 工具按钮样式字符串
     * @note 使用位置: 
     *   - BaseCaptureToolBar: 矩形、箭头、画笔、文本、马赛克、橡皮擦按钮
     *   - RecordingToolBar: 录屏按钮、截图按钮
     * @author chiangyang
     */
    static QString getToolButtonStyle() {
        return QString(
                    "QPushButton { color: %1; background-color: %2; padding: 0.24em; border: none; border-radius: 0.24em; transition: all 0.2s ease; }"
                    "QPushButton:hover { background-color: %3; transform: scale(1.05); }"
                    "QPushButton::icon { color: %4; }"
                ).arg(s_toolbarTextColor.name())
                .arg(s_toolbarBtnColor.name())
                .arg(s_toolbarButtonHoverColor.name())
                .arg(s_toolbarBtnColor.name());
    }

    /**
     * @brief 获取操作按钮样式（包括悬停、选中、禁用状态）
     * @return 操作按钮样式字符串
     * @note 使用位置: 
     *   - BaseCaptureToolBar: 撤销、重做、清除按钮
     *   - ScreenshotToolBar: 录制、贴图、复制
     *   - RecordingControlWindow: 开始、暂停、继续、停止
     * @author chiangyang
     */
    static QString getActionButtonStyle() {
        return QString(
            "QPushButton { color: %1; background-color: %2; padding: 0.24em; border: none; border-radius: 0.24em; transition: all 0.2s ease; }"
            "QPushButton:checked { background-color: %3; }"
            "QPushButton:hover { background-color: %4; transform: scale(1.05); }"
            "QPushButton:disabled { color: %1; background-color: %5; }"
            "QPushButton::icon { color: %1; }"
        ).arg(s_toolbarTextColor.name())
         .arg(s_toolbarBtnColor.name())
         .arg(s_toolbarButtonCheckedColor.name())
         .arg(s_toolbarButtonHoverColor.name())
         .arg(s_toolbarButtonDisabledColor.name());
    }

    /**
     * @brief 获取关闭按钮样式
     * @return 关闭按钮样式字符串
     * @note 使用位置: 
     *   - ScreenshotToolBar: 关闭按钮（红色背景）
     *   - RecordingToolBar: 取消录屏按钮
     * @author chiangyang
     */
    static QString getCloseButtonStyle() {
        return QString(
            "QPushButton { color: %1; background-color: %2; padding: 0.24em; border: none; border-radius: 0.24em; transition: all 0.2s ease; }")
            .arg(s_toolbarTextColor.name())
            .arg(s_closeButtonBgColor.name()) +
            QString(
            "QPushButton:hover { background-color: %1; transform: scale(1.05); }")
            .arg(s_closeButtonHoverColor.name()) +
            QString("QPushButton::icon { color: %1; }")
            .arg(s_toolbarTextColor.name());
    }

    /**
     * @brief 应用工具按钮样式到指定按钮
     * @param button 目标按钮
     * @note 使用位置: BaseCaptureToolBar、RecordingToolBar 中的工具按钮
     * @author chiangyang
     */
    static void applyToolButtonStyle(QPushButton *button) {
        if (button) {
            button->setStyleSheet(getToolButtonStyle());
        }
    }

    /**
     * @brief 应用操作按钮样式到指定按钮
     * @param button 目标按钮
     * @note 使用位置: BaseCaptureToolBar、ScreenshotToolBar、RecordingControlWindow 中的操作按钮
     * @author chiangyang
     */
    static void applyActionButtonStyle(QPushButton *button) {
        if (button) {
            button->setStyleSheet(getActionButtonStyle());
        }
    }

    /**
     * @brief 应用关闭按钮样式到指定按钮
     * @param button 目标按钮
     * @note 使用位置: ScreenshotToolBar、RecordingToolBar 中的关闭/取消按钮
     * @author chiangyang
     */
    static void applyCloseButtonStyle(QPushButton *button) {
        if (button) {
            button->setStyleSheet(getCloseButtonStyle());
        }
    }

    /**
     * @brief 应用普通按钮样式到指定按钮（包含所有状态：普通、悬停、选中、禁用）
     * @param button 目标按钮
     * @note 使用位置: RecordingControlWindow 中的开始、暂停、继续、停止、浏览按钮
     * @note 内部调用 getToolButtonStyle，统一所有按钮样式
     * @author chiangyang
     */
    static void applyNormalButtonStyle(QPushButton *button) {
        if (button) {
            button->setStyleSheet(getActionButtonStyle());
        }
    }

    /**
     * @brief 获取按钮选中状态样式
     * @return 选中状态样式字符串
     * @note 使用位置: BaseCaptureToolBar（工具按钮选中时的高亮效果）
     * @author chiangyang
     */
    static QString getButtonCheckedStyle() {
        return QString("color: %1; background-color: %2; padding: 0.24em; border: none; border-radius: 0.24em;")
            .arg(s_toolbarTextColor.name())
            .arg(s_toolbarButtonCheckedColor.name());
    }

    /**
     * @brief 获取下拉框样式
     * @return 下拉框样式字符串
     * @note 使用位置: 
     *   - BaseToolBar: 形状类型选择下拉框
     *   - RecordingControlWindow: 分辨率选择下拉框
     * @author chiangyang
     */
    static QString getComboBoxStyle() {
        return QString(
            "QComboBox { color: %1; background-color: %2; border: 1px solid %3; padding: 0.14em 0.19em; border-radius: 0.24em; }"
            "QComboBox::drop-down { border: none; }"
            "QComboBox::down-arrow { image: none; border-left: 0.24em solid transparent; border-right: 0.24em solid transparent; border-top: 0.29em solid %1; }"
            "QComboBox QAbstractItemView { background-color: %2; color: %1; border: 1px solid %3; }"
            "QComboBox QAbstractItemView::item { padding: 0.24em; }"
            "QComboBox QAbstractItemView::item:hover { background-color: #444; }"
            "QComboBox QAbstractItemView::item:selected { background-color: %4; color: %1; }"
        ).arg(s_toolbarTextColor.name())
         .arg(s_toolbarBtnColor.name())
         .arg("#333")
         .arg(s_toolbarButtonCheckedColor.name());
    }

    /**
     * @brief 获取复选框样式
     * @return 复选框样式字符串
     * @note 使用位置:
     *   - RecordingControlWindow: 音频录制选项复选框
     * @author chiangyang
     */
    static QString getCheckBoxStyle() {
        return QString(
            "QCheckBox { color: %1; spacing: 4px; }"
            "QCheckBox::indicator { width: 0.67em; height: 0.67em; }"
        ).arg(s_toolbarTextColor.name());
    }

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
    static QString getSettingsCheckBoxStyle() {
        return QString(
            "QCheckBox { color: #000; }"
            "QCheckBox::indicator { width: 1.1em; height: 1.1em; }"
        );
    }

    /**
     * @brief 获取颜色按钮样式
     * @param color 颜色值
     * @param isSelected 是否选中
     * @return 颜色按钮样式字符串
     * @note 使用位置: BaseToolBar（颜色选择器中的颜色按钮，选中时有边框效果）
     * @author chiangyang
     */
    static QString getColorButtonStyle(const QString &color, bool isSelected = false) {
        if (isSelected) {
            return QString("background-color: %1; border: 2px solid #000;").arg(color);
        }
        return QString("background-color: %1; border: 1px solid #ccc;").arg(color);
    }

    /**
     * @brief 获取截图文本编辑框样式
     * @param color 文字和边框颜色
     * @param fontSize 字体像素大小
     * @return 截图文本编辑框样式字符串
     * @note 使用位置: OverlayTextEdit（截图上的浮动文本输入框）
     * @author chiangyang
     */
    static QString getOverlayTextEditStyle(const QColor &color, int fontSize) {
        return QString("QTextEdit { background-color: rgba(255, 255, 255, 200); "
                       "border: 1px dashed %1; color: %1; font-size: %2px; }")
            .arg(color.name())
            .arg(fontSize);
    }

    /**
     * @brief 获取选区尺寸信息标签样式
     * @return 选区尺寸信息标签样式字符串
     * @note 使用位置: Selector（选区左上角的尺寸提示标签）
     * @author chiangyang
     */
    static QString getSnipInfoLabelStyle() {
        return "background-color: rgba(0,0,0,150); color: white; "
               "padding: 0.14em 0.38em; border-radius: 0.19em; font-size: 10pt;";
    }

    /**
     * @brief 获取录制时间标签样式
     * @return 录制时间标签样式字符串
     * @note 使用位置: Selector（选区左上角的录制时间标签，显示在尺寸标签上方）
     * @author chiangyang
     */
    static QString getRecordTimerLabelStyle() {
        return "background-color: rgba(0,0,0,150); color: #ff4444; "
               "font-size: 10pt; font-weight: bold; padding: 0.1em 0.38em; border-radius: 0.14em;";
    }

    /**
     * @brief 获取PinWindow首次提示标签样式
     * @return PinWindow首次提示标签样式字符串
     * @note 使用位置: PinWindow（首次显示时的操作提示标签）
     * @author chiangyang
     */
    static QString getPinHintLabelStyle() {
        return "background-color: rgba(0, 0, 0, 180); color: white; padding: 0.38em 0.57em; border-radius: 0.19em; font-size: 10pt;";
    }

    /**
     * @brief 获取OCR识别加载提示标签样式
     * @return OCR识别加载提示标签样式字符串
     * @note 使用位置: 
     *   - SnipScreen（截图模式下OCR识别中的加载提示）
     *   - PinWindow（PinWindow模式下OCR识别中的加载提示）
     * @author chiangyang
     */
    static QString getOcrLoadingLabelStyle() {
        return "background-color: rgba(0,0,0,180); color: white; padding: 0.57em 0.95em; border-radius: 0.29em; font-size: 10pt;";
    }

    /**
     * @brief 获取OCR结果文本显示框样式
     * @return OCR结果文本显示框样式字符串
     * @note 使用位置: OcrResultDialog（OCR识别结果文本显示区域）
     * @author chiangyang
     */
    static QString getOcrResultTextStyle() {
        return QString(
            "QTextEdit {"
            "    background-color: #ffffff;"
            "    color: #333333;"
            "    border: 1px solid #cccccc;"
            "    border-radius: 0.3em;"
            "    padding: 0.5em;"
            "    font-size: 14pt;"
            "}"
        );
    }

    /**
     * @brief 获取OCR结果标题样式
     * @return OCR结果标题样式字符串
     * @note 使用位置: OcrResultDialog（OCR识别结果标题）
     * @author chiangyang
     */
    static QString getOcrResultTitleStyle() {
        return "font-size: 10pt; font-weight: bold;";
    }

    /**
     * @brief 获取应用名称标签样式
     * @return 应用名称标签样式字符串
     * @note 使用位置: SettingsWindow（设置窗口中的应用名称标签）
     * @author chiangyang
     */
    static QString getAppNameLabelStyle() {
        return "font-size: 9pt; font-weight: bold;";
    }

    /**
     * @brief 获取菜单样式
     * @return 菜单样式字符串
     * @note 使用位置: PinWindow（右键菜单）、托盘区
     * @author chiangyang
     */
    static QString getMenuStyle() {
        return QString(
            "QMenu { background-color: #ffffff; color: #000000; border: 1px solid #cccccc; border-radius: 0.29em; padding: 0.29em 0; font-size: 10pt; }"
            "QMenu::item { padding: 0.48em 0.71em; margin: 0 0.29em; border-radius: 0.19em; }"
            "QMenu::item:selected { background-color: #e0e0e0; }"
            "QMenu::item:hover { background-color: #e0e0e0; }"
            "QMenu::separator { height: 0.1em; background-color: #cccccc; margin: 0.3em 0.5em; }"
        );
    }

    /**
     * @brief 获取窗口样式（带文字颜色）
     * @return 窗口样式字符串
     * @note 使用位置: RecordingControlWindow（录制控制窗口）
     * @author chiangyang
     */
    static QString getWindowStyle() {
        return QString("background-color: %1; border-radius: 0.24em; color: %2;")
            .arg(s_recordControlBgColor.name())
            .arg(s_toolbarTextColor.name());
    }
    
    /**
     * @brief 获取路径输入框样式
     * @return 路径输入框样式字符串
     * @note 使用位置: SettingsWindow（保存路径显示）
     * @author chiangyang
     */
    static QString getPathEditStyle() {
        return QString(
            "QLineEdit {"
            "    border: 1px solid #CCCCCC;"
            "    border-bottom: 1px solid #848484;"
            "    background-color: #fff;"
            "    color: #000;"
            "}"
        );
    }
    
    /**
     * @brief 获取快捷键输入框样式
     * @return 快捷键输入框样式字符串
     * @note 使用位置: SettingsWindow（快捷键设置）
     * @author chiangyang
     */
    static QString getKeySequenceEditStyle() {
        return QString(
            "QKeySequenceEdit QLineEdit {"
            "    border: 1px solid #CCCCCC;"
            "    border-bottom: 1px solid #848484;"
            "    background-color: #fff;"
            "    color: #000;"
            "}"

            "QKeySequenceEdit QLineEdit:focus {"
            "    border: 1px solid #CCCCCC;"
            "    border-bottom: 2px solid #1E90FF; /* 蓝色下边框 */"
            "}"
        );
    }

    /**
     * @brief 获取分组框样式
     * @return 分组框样式字符串
     * @note 尺寸（border-radius/margin/padding）由全局 qss 用 em 单位管理，
     *       此处只返回颜色相关样式（用户可配置的背景色）
     * @author chiangyang
     */
    static QString getGroupBoxStyle() {
        return QString(
            "QGroupBox { background-color: %1; color: #000; }"
        ).arg(s_tabWidgetBgColor.name());
    }
    
    /**
     * @brief 获取设置窗口按钮样式
     * @return 设置窗口按钮样式字符串
     * @note 使用位置: SettingsWindow（快捷键设置按钮）
     *       尺寸由全局 qss 用 em 单位管理，此处只返回颜色相关样式
     * @author chiangyang
     */
    static QString getSettingsButtonStyle() {
        return QString(
            "QPushButton { color: %1; background-color: %2; border: none; }"
            "QPushButton:hover { background-color: %4; }"
            "QPushButton:disabled { color: %1; background-color: %3; }"
        ).arg(s_settingButtonTextColor.name())
         .arg(s_settingButtonBgColor.name())
         .arg(s_toolbarButtonDisabledColor.name())
         .arg(s_toolbarButtonHoverColor.name());
    }

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
    static QString getSettingsComboBoxStyle() {
        return QString(
            "QComboBox { background-color: #fff; color: #000; }"
            "QComboBox::drop-down { border: none; }"
            "QComboBox::down-arrow { image: none; border-left: 0.24em solid transparent; border-right: 0.24em solid transparent; border-top: 0.29em solid #000; }"
            "QComboBox QAbstractItemView { background-color: #fff; color: #000; border: 1px solid #848484; outline: none; }"
            "QComboBox QAbstractItemView::item { padding: 0.24em; }"
            "QComboBox QAbstractItemView::item:hover { background-color: #d0d0d0; }"
            "QComboBox QAbstractItemView::item:selected { background-color: #d0d0d0; color: #000; }"
            "QAbstractSpinBox { background-color: #fff; color: #000; border: 1px solid #848484; border-radius: 0.24em; padding: 0.14em 0.19em; }"
            "QAbstractSpinBox::up-button, QAbstractSpinBox::down-button { width: 1.2em; border: none; background: transparent; }"
            "QAbstractSpinBox::up-button { subcontrol-origin: border; subcontrol-position: top right; }"
            "QAbstractSpinBox::down-button { subcontrol-origin: border; subcontrol-position: bottom right; }"
            "QAbstractSpinBox::up-arrow { image: url(:/icons/spinner-up.svg); width: 0.5em; height: 0.5em; }"
            "QAbstractSpinBox::down-arrow { image: url(:/icons/spinner-down.svg); width: 0.5em; height: 0.5em; }"
        );
    }

    /**
     * @brief 获取消息框样式
     * @return 消息框样式字符串
     * @note 使用位置: 项目所有 QMessageBox（确认对话框、警告、信息提示、翻译隐私提示等）
     *       统一消息框视觉风格，背景与 GroupBox 一致，按钮配色参考设置窗口按钮
     *       尺寸由全局 qss 用 em 单位管理，此处只返回颜色相关样式
     * @author chiangyang
     */
    static QString getMessageBoxStyle() {
        return QString(
            "QMessageBox { background-color: %1; }"
            "QMessageBox QLabel { color: #333333; }"
            "QMessageBox QPushButton { color: %2; background-color: %3; border: none; }"
            "QMessageBox QPushButton:hover { background-color: %4; }"
            "QMessageBox QPushButton:pressed { background-color: %4; }"
            "QMessageBox QPushButton:disabled { color: %2; background-color: %5; }"
        ).arg(s_tabWidgetBgColor.name())
         .arg(s_settingButtonTextColor.name())
         .arg(s_settingButtonBgColor.name())
         .arg(s_toolbarButtonHoverColor.name())
         .arg(s_toolbarButtonDisabledColor.name());
    }
    
    /**
     * @brief 获取进度条样式
     * @return 进度条样式字符串
     * @note 使用位置: SettingsWindow（检查更新下载进度）
     *       尺寸由全局 qss 用 em 单位管理，此处只返回颜色相关样式
     *       进度条填充色与设置按钮悬停色保持一致，背景色与选项卡背景一致
     * @author chiangyang
     */
    static QString getProgressBarStyle() {
        return QString(
            "QProgressBar { border: none; background-color: %1; }"
            "QProgressBar::chunk { background-color: %2; border-radius: 3px; }"
        ).arg(s_tabWidgetBgColor.name())
         .arg(s_toolbarButtonHoverColor.name());
    }
    
    /**
     * @brief 获取选项卡控件样式
     * @return 选项卡控件样式字符串
     * @note 使用位置: SettingsWindow（选项卡控件）、HistoryWindow（全部/截图/文本选项卡）
     *       尺寸由全局 qss 用 em 单位管理，此处只返回颜色相关样式
     * @author chiangyang
     */
    static QString getTabWidgetStyle() {
        return QString(
            "QTabWidget::pane { background-color: %1; }"
            "QTabBar::tab { background-color: %2; color: %3; }"
            "QTabBar::tab:selected { background-color: %4; color: %5; }"
            "QTabBar::tab:hover { background-color: %6; }"
        ).arg(s_tabWidgetBgColor.name())
         .arg(s_tabButtonBgColor.name())
         .arg(s_tabButtonTextColor.name())
         .arg(s_tabButtonSelectedBgColor.name())
         .arg(s_tabButtonSelectedTextColor.name())
         .arg(s_toolbarButtonHoverColor.name());
    }

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

    // 颜色设置方法
    
    /**
     * @brief 获取录屏框颜色
     * @return 录屏框颜色
     * @author chiangyang
     */
    static QColor getRecordBorderColor() {
        return s_recordBorderColor;
    }
    
    /**
     * @brief 设置录屏框颜色
     * @param color 录屏框颜色
     * @author chiangyang
     */
    static void setRecordBorderColor(const QColor &color) {
        s_recordBorderColor = color;
    }
    
    /**
     * @brief 获取截屏框颜色
     * @return 截屏框颜色
     * @author chiangyang
     */
    static QColor getCaptureBorderColor() {
        return s_captureBorderColor;
    }
    
    /**
     * @brief 设置截屏框颜色
     * @param color 截屏框颜色
     * @author chiangyang
     */
    static void setCaptureBorderColor(const QColor &color) {
        s_captureBorderColor = color;
    }
    
    /**
     * @brief 获取工具栏背景颜色
     * @return 工具栏背景颜色
     * @author chiangyang
     */
    static QColor getToolbarBgColor() {
        return s_toolbarBgColor;
    }
    
    /**
     * @brief 设置工具栏背景颜色
     * @param color 工具栏背景颜色
     * @author chiangyang
     */
    static void setToolbarBgColor(const QColor &color) {
        s_toolbarBgColor = color;
    }
    
    /**
     * @brief 获取录屏控制栏背景颜色
     * @return 录屏控制栏背景颜色
     * @author chiangyang
     */
    static QColor getRecordControlBgColor() {
        return s_recordControlBgColor;
    }
    
    /**
     * @brief 设置录屏控制栏背景颜色
     * @param color 录屏控制栏背景颜色
     * @author chiangyang
     */
    static void setRecordControlBgColor(const QColor &color) {
        s_recordControlBgColor = color;
    }
    
    /**
     * @brief 获取工具栏按钮颜色
     * @return 工具栏按钮颜色
     * @author chiangyang
     */
    static QColor getToolbarBtnColor() {
        return s_toolbarBtnColor;
    }
    
    /**
     * @brief 设置工具栏按钮颜色
     * @param color 工具栏按钮颜色
     * @author chiangyang
     */
    static void setToolbarBtnColor(const QColor &color) {
        s_toolbarBtnColor = color;
    }
    
    /**
     * @brief 获取工具栏文字颜色
     * @return 工具栏文字颜色
     * @author chiangyang
     */
    static QColor getToolbarTextColor() {
        return s_toolbarTextColor;
    }
    
    /**
     * @brief 设置工具栏文字颜色
     * @param color 工具栏文字颜色
     * @author chiangyang
     */
    static void setToolbarTextColor(const QColor &color) {
        s_toolbarTextColor = color;
    }
    
    /**
     * @brief 获取工具栏按钮悬停颜色
     * @return 工具栏按钮悬停颜色
     * @author chiangyang
     */
    static QColor getToolbarButtonHoverColor() {
        return s_toolbarButtonHoverColor;
    }
    
    /**
     * @brief 设置工具栏按钮悬停颜色
     * @param color 工具栏按钮悬停颜色
     * @author chiangyang
     */
    static void setToolbarButtonHoverColor(const QColor &color) {
        s_toolbarButtonHoverColor = color;
    }
    
    /**
     * @brief 获取工具栏按钮禁用颜色
     * @return 工具栏按钮禁用颜色
     * @author chiangyang
     */
    static QColor getToolbarButtonDisabledColor() {
        return s_toolbarButtonDisabledColor;
    }
    
    /**
     * @brief 设置工具栏按钮禁用颜色
     * @param color 工具栏按钮禁用颜色
     * @author chiangyang
     */
    static void setToolbarButtonDisabledColor(const QColor &color) {
        s_toolbarButtonDisabledColor = color;
    }
    
    /**
     * @brief 获取子工具栏背景颜色
     * @return 子工具栏背景颜色
     * @author chiangyang
     */
    static QColor getSubToolbarBgColor() {
        return s_subToolbarBgColor;
    }
    
    /**
     * @brief 设置子工具栏背景颜色
     * @param color 子工具栏背景颜色
     * @author chiangyang
     */
    static void setSubToolbarBgColor(const QColor &color) {
        s_subToolbarBgColor = color;
    }
    
    /**
     * @brief 获取设置按钮背景颜色
     * @return 设置按钮背景颜色
     * @author chiangyang
     */
    static QColor getSettingButtonBgColor() {
        return s_settingButtonBgColor;
    }
    
    /**
     * @brief 设置设置按钮背景颜色
     * @param color 设置按钮背景颜色
     * @author chiangyang
     */
    static void setSettingButtonBgColor(const QColor &color) {
        s_settingButtonBgColor = color;
    }
    
    /**
     * @brief 获取设置按钮文字颜色
     * @return 设置按钮文字颜色
     * @author chiangyang
     */
    static QColor getSettingButtonTextColor() {
        return s_settingButtonTextColor;
    }
    
    /**
     * @brief 设置设置按钮文字颜色
     * @param color 设置按钮文字颜色
     * @author chiangyang
     */
    static void setSettingButtonTextColor(const QColor &color) {
        s_settingButtonTextColor = color;
    }
    
    /**
     * @brief 获取工具栏按钮选中颜色
     * @return 工具栏按钮选中颜色
     * @author chiangyang
     */
    static QColor getToolbarButtonCheckedColor() {
        return s_toolbarButtonCheckedColor;
    }
    
    /**
     * @brief 设置工具栏按钮选中颜色
     * @param color 工具栏按钮选中颜色
     * @author chiangyang
     */
    static void setToolbarButtonCheckedColor(const QColor &color) {
        s_toolbarButtonCheckedColor = color;
    }
    
    /**
     * @brief 获取关闭按钮背景颜色
     * @return 关闭按钮背景颜色
     * @author chiangyang
     */
    static QColor getCloseButtonBgColor() {
        return s_closeButtonBgColor;
    }
    
    /**
     * @brief 设置关闭按钮背景颜色
     * @param color 关闭按钮背景颜色
     * @author chiangyang
     */
    static void setCloseButtonBgColor(const QColor &color) {
        s_closeButtonBgColor = color;
    }
    
    /**
     * @brief 获取关闭按钮悬停颜色
     * @return 关闭按钮悬停颜色
     * @author chiangyang
     */
    static QColor getCloseButtonHoverColor() {
        return s_closeButtonHoverColor;
    }
    
    /**
     * @brief 设置关闭按钮悬停颜色
     * @param color 关闭按钮悬停颜色
     * @author chiangyang
     */
    static void setCloseButtonHoverColor(const QColor &color) {
        s_closeButtonHoverColor = color;
    }
    
    /**
     * @brief 获取选项卡背景颜色
     * @return 选项卡背景颜色
     * @author chiangyang
     */
    static QColor getTabWidgetBgColor() {
        return s_tabWidgetBgColor;
    }
    
    /**
     * @brief 设置选项卡背景颜色
     * @param color 选项卡背景颜色
     * @author chiangyang
     */
    static void setTabWidgetBgColor(const QColor &color) {
        s_tabWidgetBgColor = color;
    }
    
    /**
     * @brief 获取选项卡按钮背景颜色
     * @return 选项卡按钮背景颜色
     * @author chiangyang
     */
    static QColor getTabButtonBgColor() {
        return s_tabButtonBgColor;
    }
    
    /**
     * @brief 设置选项卡按钮背景颜色
     * @param color 选项卡按钮背景颜色
     * @author chiangyang
     */
    static void setTabButtonBgColor(const QColor &color) {
        s_tabButtonBgColor = color;
    }
    
    /**
     * @brief 获取选项卡按钮文字颜色
     * @return 选项卡按钮文字颜色
     * @author chiangyang
     */
    static QColor getTabButtonTextColor() {
        return s_tabButtonTextColor;
    }
    
    /**
     * @brief 设置选项卡按钮文字颜色
     * @param color 选项卡按钮文字颜色
     * @author chiangyang
     */
    static void setTabButtonTextColor(const QColor &color) {
        s_tabButtonTextColor = color;
    }
    
    /**
     * @brief 获取选项卡按钮选中背景颜色
     * @return 选项卡按钮选中背景颜色
     * @author chiangyang
     */
    static QColor getTabButtonSelectedBgColor() {
        return s_tabButtonSelectedBgColor;
    }
    
    /**
     * @brief 设置选项卡按钮选中背景颜色
     * @param color 选项卡按钮选中背景颜色
     * @author chiangyang
     */
    static void setTabButtonSelectedBgColor(const QColor &color) {
        s_tabButtonSelectedBgColor = color;
    }
    
    /**
     * @brief 获取选项卡按钮选中文字颜色
     * @return 选项卡按钮选中文字颜色
     * @author chiangyang
     */
    static QColor getTabButtonSelectedTextColor() {
        return s_tabButtonSelectedTextColor;
    }

    /**
     * @brief 设置选项卡按钮选中文字颜色
     * @param color 选项卡按钮选中文字颜色
     * @author chiangyang
     */
    static void setTabButtonSelectedTextColor(const QColor &color) {
        s_tabButtonSelectedTextColor = color;
    }

    /**
     * @brief 获取角手柄圆形颜色
     * @return 角手柄圆形颜色
     * @author chiangyang
     */
    static QColor getHandleCircleColor() {
        return s_handleCircleColor;
    }

    /**
     * @brief 设置角手柄圆形颜色
     * @param color 角手柄圆形颜色
     * @author chiangyang
     */
    static void setHandleCircleColor(const QColor &color) {
        s_handleCircleColor = color;
    }

    /**
     * @brief 获取角手柄关闭按钮颜色
     * @return 角手柄关闭按钮颜色
     * @author chiangyang
     */
    static QColor getHandleCloseColor() {
        return s_handleCloseColor;
    }

    /**
     * @brief 设置角手柄关闭按钮颜色
     * @param color 角手柄关闭按钮颜色
     * @author chiangyang
     */
    static void setHandleCloseColor(const QColor &color) {
        s_handleCloseColor = color;
    }

    /**
     * @brief 获取工具栏按钮样式
     * @return 工具栏按钮样式（"text"或"icon"）
     * @author chiangyang
     */
    static QString getToolbarButtonStyle() {
        return s_toolbarButtonStyle;
    }
    
    /**
     * @brief 设置工具栏按钮样式
     * @param style 工具栏按钮样式（"text"或"icon"）
     * @author chiangyang
     */
    static void setToolbarButtonStyle(const QString &style) {
        s_toolbarButtonStyle = style;
    }

    /**
     * @brief 获取画笔默认粗细
     * @return 画笔默认粗细值（像素）
     * @author chiangyang
     */
    static int getDefaultPenWidth() {
        return s_defaultPenWidth;
    }

    /**
     * @brief 设置画笔默认粗细
     * @param width 画笔默认粗细值（像素）
     * @author chiangyang
     */
    static void setDefaultPenWidth(int width) {
        s_defaultPenWidth = width;
    }

    /**
     * @brief 获取文本默认字号
     * @return 文本默认字号（像素）
     * @author chiangyang
     */
    static int getDefaultFontSize() {
        return s_defaultFontSize;
    }

    /**
     * @brief 设置文本默认字号
     * @param size 文本默认字号（像素）
     * @author chiangyang
     */
    static void setDefaultFontSize(int size) {
        s_defaultFontSize = size;
    }

    /**
     * @brief 获取橡皮擦默认粗细
     * @return 橡皮擦默认粗细值（像素）
     * @author chiangyang
     */
    static int getDefaultEraserWidth() {
        return s_defaultEraserWidth;
    }

    /**
     * @brief 设置橡皮擦默认粗细
     * @param width 橡皮擦默认粗细值（像素）
     * @author chiangyang
     */
    static void setDefaultEraserWidth(int width) {
        s_defaultEraserWidth = width;
    }

    /**
     * @brief 获取马赛克默认大小
     * @return 马赛克默认大小值（像素）
     * @author chiangyang
     */
    static int getDefaultMosaicSize() {
        return s_defaultMosaicSize;
    }

    /**
     * @brief 设置马赛克默认大小
     * @param size 马赛克默认大小值（像素）
     * @author chiangyang
     */
    static void setDefaultMosaicSize(int size) {
        s_defaultMosaicSize = size;
    }
    
    /**
     * @brief 重置样式为默认值
     * @author chiangyang
     */
    static void resetToDefaults() {
        // 标注工具默认值
        s_defaultPenWidth = DEFAULT_PEN_WIDTH;
        s_defaultFontSize = DEFAULT_FONT_SIZE;
        s_defaultEraserWidth = DEFAULT_ERASER_WIDTH;
        s_defaultMosaicSize = DEFAULT_MOSAIC_SIZE;

        s_recordBorderColor = DEFAULT_RECORD_BORDER_COLOR;
        s_captureBorderColor = DEFAULT_CAPTURE_BORDER_COLOR;
        s_toolbarBgColor = DEFAULT_TOOLBAR_BG_COLOR;
        s_recordControlBgColor = DEFAULT_RECORD_CONTROL_BG_COLOR;
        s_toolbarBtnColor = DEFAULT_TOOLBAR_BTN_COLOR;
        s_toolbarTextColor = DEFAULT_TOOLBAR_TEXT_COLOR;
        s_toolbarButtonHoverColor = DEFAULT_TOOLBAR_BUTTON_HOVER_COLOR;
        s_toolbarButtonDisabledColor = DEFAULT_TOOLBAR_BUTTON_DISABLED_COLOR;
        s_subToolbarBgColor = DEFAULT_SUB_TOOLBAR_BG_COLOR;
        s_settingButtonBgColor = DEFAULT_SETTING_BUTTON_BG_COLOR;
        s_settingButtonTextColor = DEFAULT_SETTING_BUTTON_TEXT_COLOR;
        s_toolbarButtonCheckedColor = DEFAULT_TOOLBAR_BUTTON_CHECKED_COLOR;
        s_closeButtonBgColor = DEFAULT_CLOSE_BUTTON_BG_COLOR;
        s_closeButtonHoverColor = DEFAULT_CLOSE_BUTTON_HOVER_COLOR;
        s_tabWidgetBgColor = DEFAULT_TAB_WIDGET_BG_COLOR;
        s_tabButtonBgColor = DEFAULT_TAB_BUTTON_BG_COLOR;
        s_tabButtonTextColor = DEFAULT_TAB_BUTTON_TEXT_COLOR;
        s_tabButtonSelectedBgColor = DEFAULT_TAB_BUTTON_SELECTED_BG_COLOR;
        s_tabButtonSelectedTextColor = DEFAULT_TAB_BUTTON_SELECTED_TEXT_COLOR;
        s_handleCircleColor = DEFAULT_HANDLE_CIRCLE_COLOR;
        s_handleCloseColor = DEFAULT_HANDLE_CLOSE_COLOR;
        s_toolbarButtonStyle = DEFAULT_TOOLBAR_BUTTON_STYLE;
    }
    
};

#endif // STYLEMANAGER_H

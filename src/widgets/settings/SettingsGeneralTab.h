#ifndef SETTINGSGENERALTAB_H
#define SETTINGSGENERALTAB_H

#include <QWidget>
#include <QGroupBox>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include "ConfigManager.h"

class Logger;

/**
 * @brief 设置窗口「通用」选项卡
 *
 * 包含语言选择、开机自启、日志、OCR 语言与 GPU 加速、截图/录屏保存目录、
 * 配置文件管理。从 SettingsWindow 拆分而来：配置读写、日志开关、OCR
 * 引擎重载等交互内聚于此；语言切换经 languageChanged 信号通知宿主
 * （宿主执行全窗口重翻译并转发给 main.cpp）。
 * @author chiangyang
 */
class SettingsGeneralTab : public QWidget {
    Q_OBJECT

public:
    explicit SettingsGeneralTab(QWidget *parent = nullptr);

    /// 加载配置到控件（语言 / 自启 / 日志 / OCR / 保存目录）
    void loadSettings();

    /**
     * @brief 语言切换时重翻译本选项卡文案
     * @author chiangyang
     */
    void retranslateUi();

    /**
     * @brief 刷新选项卡内控件的样式（DPI / 样式重载时由宿主调用）
     * @author chiangyang
     */
    void refreshStyles();

    /**
     * @brief 应用选项卡背景色（TabWidgetBg 颜色变更时由宿主调用）
     * @param color 新的背景色
     * @author chiangyang
     */
    void applyTabBackground(const QColor &color);

    /**
     * @brief 重新计算标签与下拉框宽度（DPI 变化时由宿主调用）
     * @author chiangyang
     */
    void updateControlWidths();

signals:
    /**
     * @brief 语言改变信号（宿主转发给 main.cpp 订阅者）
     * @param langCode 语言代码
     * @author chiangyang
     */
    void languageChanged(const QString &langCode);

private slots:
    void onLanguageChanged(int index);
    void onAutoStartChanged(Qt::CheckState state);
    void onLogPrintChanged(Qt::CheckState state);
    void onOcrLanguageChanged(int index);
    void onGpuAccelChanged(Qt::CheckState state);
    void onOpenLogFile();
    void onClearLogFile();
    void onChooseCaptureSaveDir();
    void onChooseRecordSaveDir();
    void onResetCaptureSaveDir();
    void onResetRecordSaveDir();
    void onOpenConfigFileLocation();
    void onChangeConfigFile();
    void onConfigChanged(bool success, const QString &message);
    void onConfigPathChanged(const QString &newPath);

private:
    void setupUi();

    QGroupBox *saveGroup;           ///< 保存路径设置组（样式刷新引用）
    QGroupBox *configGroup;         ///< 配置文件管理组
    QComboBox *langCombo;           ///< 语言下拉框
    QCheckBox *autoStartCheck;      ///< 自动启动复选框
    QCheckBox *logPrintCheck;       ///< 日志打印复选框
    QLabel *lblLanguage;            ///< 语言标签
    QPushButton *btnOpenLogFile;    ///< 打开日志文件按钮
    QPushButton *btnClearLogFile;   ///< 清除日志文件按钮
    QLabel *lblOcrLanguage;         ///< OCR 语言标签
    QComboBox *cmbOcrLanguage;      ///< OCR 语言下拉框
    QCheckBox *gpuAccelCheck;       ///< GPU 加速复选框
    QLabel *lblCaptureSaveDir;      ///< 截屏保存路径标签
    QLineEdit *editCaptureSaveDir;  ///< 截屏保存路径
    QPushButton *btnCaptureChooseDir; ///< 选择截屏保存路径按钮
    QPushButton *btnCaptureResetDir;  ///< 恢复截屏默认路径按钮
    QLabel *lblRecordSaveDir;       ///< 录屏保存路径标签
    QLineEdit *editRecordSaveDir;   ///< 录屏保存路径
    QPushButton *btnRecordChooseDir; ///< 选择录屏保存路径按钮
    QPushButton *btnRecordResetDir;  ///< 恢复录屏默认路径按钮

    // Config File Management
    QLabel *lblConfigTitle;       ///< 配置文件标题标签
    QLineEdit *lblConfigFileName; ///< 配置文件路径文本框
    QPushButton *btnOpenConfigDir; ///< 打开配置文件所在文件夹按钮
    QPushButton *btnChangeConfig;   ///< 更改配置文件按钮
    ConfigManager *m_configManager; ///< 配置文件管理器
};

#endif // SETTINGSGENERALTAB_H
